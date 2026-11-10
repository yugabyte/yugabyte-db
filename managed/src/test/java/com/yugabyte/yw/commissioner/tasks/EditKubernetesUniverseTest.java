// Copyright (c) YugabyteDB, Inc.

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
import static org.hamcrest.CoreMatchers.containsString;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertThrows;
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
import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.ITask;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.RegexMatcher;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ExposingServiceState;
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
import java.util.Collections;
import java.util.Comparator;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.annotation.Nullable;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.tuple.ImmutablePair;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.junit.MockitoJUnitRunner;
import org.yaml.snakeyaml.Yaml;
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
    when(mockYBClient.getUniverseClient(any())).thenReturn(mockClient);
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
          TaskType.UpdateConsistencyCheck,
          TaskType.FreezeUniverse,
          TaskType.KubernetesCommandExecutor,
          TaskType.KubernetesCheckNumPod,
          TaskType.KubernetesCommandExecutor,
          TaskType.WaitForServer,
          TaskType.SwamperTargetsFileUpdate,
          TaskType.UpdatePlacementInfo,
          TaskType.HandleKubernetesNamespacedServices,
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
          TaskType.UpdateConsistencyCheck,
          TaskType.FreezeUniverse,
          TaskType.UpdatePlacementInfo,
          TaskType.WaitForDataMove,
          TaskType.HandleKubernetesNamespacedServices,
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
          TaskType.UpdateConsistencyCheck,
          TaskType.FreezeUniverse,
          TaskType.UpdatePlacementInfo,
          TaskType.HandleKubernetesNamespacedServices,
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

  // Reads the "partition" map from the helm override file that was passed to helmUpgrade(...).
  @SuppressWarnings("unchecked")
  private Map<String, Object> readPartitionOverrides(String overrideFilePath) {
    try (InputStream is = new FileInputStream(new File(overrideFilePath))) {
      Map<String, Object> overrides = new Yaml().loadAs(is, Map.class);
      return (Map<String, Object>) overrides.get("partition");
    } catch (IOException e) {
      throw new RuntimeException("Could not read helm override file: " + overrideFilePath, e);
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

    // Tserver scale-up (master count unchanged) is a non-rolling operation: it must not set
    // partition to 0, otherwise a prior non-restart template change would roll all existing
    // master/tserver pods at once. Partition must protect the existing pods.
    Map<String, Object> partition = readPartitionOverrides(expectedOverrideFile.getValue());
    assertEquals(1, partition.get("master"));
    assertEquals(3, partition.get("tserver"));

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

    // Tserver scale-down (keepDeployment) is a non-rolling operation: it must not set partition
    // to 0, otherwise a prior non-restart template change would roll all surviving master/tserver
    // pods at once. Partition must equal the post-scale-down pod counts.
    Map<String, Object> partition = readPartitionOverrides(expectedOverrideFile.getValue());
    assertEquals(1, partition.get("master"));
    assertEquals(2, partition.get("tserver"));

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

  private Object readEnableLoadBalancer(String overrideFilePath) {
    try (InputStream is = new FileInputStream(new File(overrideFilePath))) {
      Map<String, Object> overrides = new Yaml().loadAs(is, Map.class);
      return overrides.get("enableLoadBalancer");
    } catch (IOException e) {
      throw new RuntimeException("Could not read helm override file: " + overrideFilePath, e);
    }
  }

  private void saveExposingService(
      ExposingServiceState primary, @Nullable ExposingServiceState readReplica) {
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            u -> {
              UniverseDefinitionTaskParams details = u.getUniverseDetails();
              details.getPrimaryCluster().userIntent.enableExposingService = primary;
              if (readReplica != null) {
                details.getReadOnlyClusters().get(0).userIntent.enableExposingService = readReplica;
              }
              u.setUniverseDetails(details);
            });
  }

  /** Adds a one-pod read replica in az-1 to the single-AZ universe. */
  private void addReadReplica() {
    UniverseDefinitionTaskParams.UserIntent rrIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    rrIntent.numNodes = 1;
    rrIntent.replicationFactor = 1;
    rrIntent.provider = kubernetesProvider.getUuid().toString();
    rrIntent.providerType = Common.CloudType.kubernetes;
    rrIntent.accessKeyCode = "demo-access";
    rrIntent.deviceInfo = ApiUtils.getDummyDeviceInfo(1, 100);
    UniverseDefinitionTaskParams.UserIntent primaryIntent =
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent;
    rrIntent.ybSoftwareVersion = primaryIntent.ybSoftwareVersion;
    rrIntent.tserverK8SNodeResourceSpec = primaryIntent.tserverK8SNodeResourceSpec;
    rrIntent.masterK8SNodeResourceSpec = primaryIntent.masterK8SNodeResourceSpec;
    rrIntent.regionList =
        Collections.singletonList(Region.getByCode(kubernetesProvider, "region-1").getUuid());
    PlacementInfo rrPlacement = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(
        AvailabilityZone.getByCode(kubernetesProvider, "az-1").getUuid(), rrPlacement, 1, 1, true);
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            ApiUtils.mockUniverseUpdaterWithReadReplica(rrIntent, rrPlacement));
    when(mockKubernetesManager.getPodInfos(any(), any(), any()))
        .thenAnswer(
            invocation ->
                String.valueOf(invocation.<String>getArgument(1)).contains("-rr")
                    ? tserverPods(1)
                    : rollbackPods(3));
  }

  /** Edits the primary cluster's expose service setting and nothing else. */
  private TaskInfo submitExposingServiceEdit(ExposingServiceState exposingService) {
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = -1;
    taskParams.nodeDetailsSet = defaultUniverse.getUniverseDetails().nodeDetailsSet;
    UniverseDefinitionTaskParams.UserIntent newUserIntent =
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.clone();
    newUserIntent.enableExposingService = exposingService;
    return submitTask(
        taskParams,
        newUserIntent,
        defaultUniverse.getUniverseDetails().getPrimaryCluster().placementInfo);
  }

  private Map<String, String> captureHelmUpgradeOverrideFiles(int times) {
    ArgumentCaptor<String> releaseName = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> overrideFile = ArgumentCaptor.forClass(String.class);
    verify(mockKubernetesManager, times(times))
        .helmUpgrade(any(), any(), any(), releaseName.capture(), any(), overrideFile.capture());
    Map<String, String> result = new HashMap<>();
    for (int i = 0; i < times; i++) {
      result.put(releaseName.getAllValues().get(i), overrideFile.getAllValues().get(i));
    }
    return result;
  }

  @Test
  public void testExposingServiceChangeUpgradesWithoutRestart() {
    setupUniverseSingleAZ(/* Create Masters */ true);
    saveExposingService(ExposingServiceState.UNEXPOSED, null);
    when(mockKubernetesManager.getPodInfos(any(), any(), any())).thenReturn(rollbackPods(3));

    TaskInfo taskInfo = submitExposingServiceEdit(ExposingServiceState.EXPOSED);
    assertEquals(describeFailure(taskInfo), Success, taskInfo.getTaskState());

    String overrideFile = captureHelmUpgradeOverrideFiles(1).get(NODE_PREFIX);
    assertEquals(true, readEnableLoadBalancer(overrideFile));
    // The partitions stay at the pod counts, so no pod is replaced.
    Map<String, Object> partition = readPartitionOverrides(overrideFile);
    assertEquals(1, partition.get("master"));
    assertEquals(3, partition.get("tserver"));
    verify(mockKubernetesManager, times(0)).deletePod(any(), any(), any());
    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertEquals(
        ExposingServiceState.EXPOSED,
        universe.getUniverseDetails().getPrimaryCluster().userIntent.enableExposingService);
  }

  @Test
  public void testExposingServiceChangeWithInstanceTypeChange() {
    setupUniverseSingleAZ(/* Create Masters */ true);
    saveExposingService(ExposingServiceState.UNEXPOSED, null);
    factory
        .forUniverse(defaultUniverse)
        .setValue("yb.checks.node_disk_size.target_usage_percentage", "0");
    when(mockKubernetesManager.getPodInfos(any(), any(), any())).thenReturn(rollbackPods(3));

    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = -1;
    taskParams.nodeDetailsSet = defaultUniverse.getUniverseDetails().nodeDetailsSet;
    InstanceType.upsert(
        kubernetesProvider.getUuid(), "c5.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());
    UniverseDefinitionTaskParams.UserIntent newUserIntent =
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.clone();
    newUserIntent.instanceType = "c5.xlarge";
    newUserIntent.tserverK8SNodeResourceSpec = new K8SNodeResourceSpec();
    newUserIntent.tserverK8SNodeResourceSpec.cpuCoreCount = 4.0;
    newUserIntent.masterK8SNodeResourceSpec = new K8SNodeResourceSpec();
    newUserIntent.enableExposingService = ExposingServiceState.EXPOSED;
    TaskInfo taskInfo =
        submitTask(
            taskParams,
            newUserIntent,
            defaultUniverse.getUniverseDetails().getPrimaryCluster().placementInfo);
    assertEquals(describeFailure(taskInfo), Success, taskInfo.getTaskState());

    // The tserver roll applies the new setting; no separate upgrade is added.
    ArgumentCaptor<String> overrideFile = ArgumentCaptor.forClass(String.class);
    verify(mockKubernetesManager, times(3))
        .helmUpgrade(any(), any(), any(), any(), any(), overrideFile.capture());
    for (String file : overrideFile.getAllValues()) {
      assertEquals(true, readEnableLoadBalancer(file));
    }
  }

  @Test
  public void testExposingServiceChangeUpgradesReadReplica() {
    setupUniverseSingleAZ(/* Create Masters */ true);
    addReadReplica();
    saveExposingService(ExposingServiceState.UNEXPOSED, ExposingServiceState.UNEXPOSED);

    TaskInfo taskInfo = submitExposingServiceEdit(ExposingServiceState.EXPOSED);
    assertEquals(describeFailure(taskInfo), Success, taskInfo.getTaskState());

    Map<String, String> overrideFiles = captureHelmUpgradeOverrideFiles(2);
    assertEquals(true, readEnableLoadBalancer(overrideFiles.get(NODE_PREFIX)));
    assertEquals(true, readEnableLoadBalancer(overrideFiles.get(NODE_PREFIX + "-rr")));
    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertEquals(
        ExposingServiceState.EXPOSED,
        universe
            .getUniverseDetails()
            .getReadOnlyClusters()
            .get(0)
            .userIntent
            .enableExposingService);
  }

  @Test
  public void testReadReplicaExposingServiceBroughtInStepWithPrimary() {
    // As left by an edit that saved the primary cluster and failed before the read replica.
    setupUniverseSingleAZ(/* Create Masters */ true);
    addReadReplica();
    saveExposingService(ExposingServiceState.EXPOSED, ExposingServiceState.UNEXPOSED);

    TaskInfo taskInfo = submitExposingServiceEdit(ExposingServiceState.EXPOSED);
    assertEquals(describeFailure(taskInfo), Success, taskInfo.getTaskState());

    Map<String, String> overrideFiles = captureHelmUpgradeOverrideFiles(1);
    assertEquals(true, readEnableLoadBalancer(overrideFiles.get(NODE_PREFIX + "-rr")));
    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertEquals(
        ExposingServiceState.EXPOSED,
        universe
            .getUniverseDetails()
            .getReadOnlyClusters()
            .get(0)
            .userIntent
            .enableExposingService);
  }

  @Test
  public void testReadReplicaExposingServiceChangeRejected() {
    setupUniverseSingleAZ(/* Create Masters */ true);
    addReadReplica();
    saveExposingService(ExposingServiceState.UNEXPOSED, ExposingServiceState.UNEXPOSED);

    Cluster readReplica = defaultUniverse.getUniverseDetails().getReadOnlyClusters().get(0);
    readReplica.userIntent.enableExposingService = ExposingServiceState.EXPOSED;
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = -1;
    taskParams.nodeDetailsSet = defaultUniverse.getUniverseDetails().nodeDetailsSet;
    taskParams.clusters = new ArrayList<>(Collections.singletonList(readReplica));
    taskParams.nodePrefix = NODE_PREFIX;

    PlatformServiceException e =
        assertThrows(
            PlatformServiceException.class,
            () -> commissioner.submit(TaskType.EditKubernetesUniverse, taskParams));
    assertEquals(BAD_REQUEST, e.getHttpStatus());
    assertThat(e.getMessage(), containsString("expose service setting follows the primary"));
  }

  //   @Test
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
    checkUniverseNodesStates(taskParams.getUniverseUUID());
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

  @Test
  public void testVolumeDecreaseForRRIsForbidden() {
    setupUniverseSingleAZ(/* Create Masters */ true);
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.numNodes = 1;
    userIntent.replicationFactor = 1;
    userIntent.deviceInfo = ApiUtils.getDummyDeviceInfo(1, 100);
    userIntent.provider = kubernetesProvider.getUuid().toString();
    userIntent.providerType = Common.CloudType.kubernetes;
    userIntent.regionList =
        Collections.singletonList(Region.getByCode(kubernetesProvider, "region-1").getUuid());
    PlacementInfo pi = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(
        AvailabilityZone.getByCode(kubernetesProvider, "az-1").getUuid(), pi, 1, 1, true);
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            ApiUtils.mockUniverseUpdaterWithReadReplica(userIntent, pi));

    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = defaultUniverse.getVersion();
    taskParams.nodeDetailsSet = defaultUniverse.getUniverseDetails().nodeDetailsSet;
    taskParams.clusters =
        Collections.singletonList(
            defaultUniverse.getUniverseDetails().getReadOnlyClusters().get(0));
    taskParams.getReadOnlyClusters().get(0).userIntent.deviceInfo.volumeSize--;
    taskParams.nodePrefix = NODE_PREFIX;
    Exception expected = null;
    try {
      UUID taskUUID = commissioner.submit(TaskType.EditKubernetesUniverse, taskParams);
      waitForTask(taskUUID);
    } catch (Exception e) {
      expected = e;
    }
    assertNotNull(expected);
    assertTrue(expected.getMessage().contains("Cannot decrease disk size in a Kubernetes cluster"));
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

  private List<Pod> rollbackPods(int numTservers) {
    StringBuilder sb = new StringBuilder("{\"items\": [");
    sb.append(rollbackPodJson("yb-master-0", "1.2.3.0"));
    for (int i = 0; i < numTservers; i++) {
      sb.append(",").append(rollbackPodJson("yb-tserver-" + i, "1.2.3." + (i + 1)));
    }
    sb.append("]}");
    return TestUtils.deserialize(sb.toString(), PodList.class).getItems();
  }

  private List<Pod> tserverPods(int numTservers) {
    StringBuilder sb = new StringBuilder("{\"items\": [");
    for (int i = 0; i < numTservers; i++) {
      sb.append(i == 0 ? "" : ",").append(rollbackPodJson("yb-tserver-" + i, "1.2.3." + (i + 1)));
    }
    sb.append("]}");
    return TestUtils.deserialize(sb.toString(), PodList.class).getItems();
  }

  private String rollbackPodJson(String hostname, String ip) {
    return "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", \"podIP\": \""
        + ip
        + "\"}, \"spec\": {\"hostname\": \""
        + hostname
        + "\"}, \"metadata\": {\"namespace\": \""
        + NODE_PREFIX
        + "\"}}";
  }

  private static String describeFailure(TaskInfo taskInfo) {
    java.util.function.Function<String, String> tail =
        m -> m == null ? "null" : m.substring(Math.max(0, m.lastIndexOf("hit error:")));
    return tail.apply(taskInfo.getErrorMessage())
        + " | failed subtasks: "
        + taskInfo.getSubTasks().stream()
            .filter(t -> t.getTaskState() == Failure)
            .map(t -> t.getTaskType() + ": " + tail.apply(t.getErrorMessage()))
            .collect(Collectors.joining("; "));
  }

  /** Adds a one-tserver read replica in az-1 to the multi-AZ universe. */
  private void addReadReplicaToMultiAZUniverse() {
    UniverseDefinitionTaskParams.UserIntent rrIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    rrIntent.numNodes = 1;
    rrIntent.replicationFactor = 1;
    rrIntent.provider = kubernetesProvider.getUuid().toString();
    rrIntent.providerType = Common.CloudType.kubernetes;
    rrIntent.accessKeyCode = "demo-access";
    rrIntent.deviceInfo = ApiUtils.getDummyDeviceInfo(1, 100);
    UniverseDefinitionTaskParams.UserIntent primaryIntent =
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent;
    rrIntent.ybSoftwareVersion = primaryIntent.ybSoftwareVersion;
    rrIntent.tserverK8SNodeResourceSpec = primaryIntent.tserverK8SNodeResourceSpec;
    rrIntent.masterK8SNodeResourceSpec = primaryIntent.masterK8SNodeResourceSpec;
    rrIntent.regionList =
        Collections.singletonList(Region.getByCode(kubernetesProvider, "region-1").getUuid());
    PlacementInfo rrPlacement = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(
        AvailabilityZone.getByCode(kubernetesProvider, "az-1").getUuid(), rrPlacement, 1, 1, true);
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            ApiUtils.mockUniverseUpdaterWithReadReplica(rrIntent, rrPlacement));
  }

  /**
   * Builds an edit request with the given primary placement. Like the UI, it also carries the
   * unchanged read replica when includeReadReplica is set; API clients may leave it out.
   */
  private UniverseDefinitionTaskParams editParams(PlacementInfo pi, boolean includeReadReplica) {
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = defaultUniverse.getVersion();
    taskParams.nodeDetailsSet = defaultUniverse.getUniverseDetails().nodeDetailsSet;
    taskParams.upsertPrimaryCluster(
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.clone(), pi);
    taskParams.nodePrefix = NODE_PREFIX;
    taskParams.getPrimaryCluster().uuid =
        defaultUniverse.getUniverseDetails().getPrimaryCluster().uuid;
    if (includeReadReplica) {
      taskParams.clusters.add(defaultUniverse.getUniverseDetails().getReadOnlyClusters().get(0));
    }
    return taskParams;
  }

  /**
   * Submits the edit, pauses it once all its subtasks are queued, then aborts it. Returns the
   * aborted task, whose subtasks are the queued ones.
   */
  private TaskInfo queueAndAbortEdit(UniverseDefinitionTaskParams taskParams)
      throws InterruptedException {
    setPausePosition(0);
    UUID taskUUID = commissioner.submit(TaskType.EditKubernetesUniverse, taskParams);
    waitForEditPaused(taskUUID);
    TaskInfo taskInfo = TaskInfo.getOrBadRequest(taskUUID);
    int freezePosition =
        taskInfo.getSubTasks().stream()
            .filter(t -> t.getTaskType() == TaskType.FreezeUniverse)
            .map(TaskInfo::getPosition)
            .findFirst()
            .orElseThrow();
    setPausePosition(freezePosition + 1);
    commissioner.resumeTask(taskUUID);
    waitForEditPaused(taskUUID);
    clearAbortOrPausePositions();
    setAbortPosition(freezePosition + 1);
    commissioner.resumeTask(taskUUID);
    taskInfo = waitForTask(taskUUID);
    clearAbortOrPausePositions();
    return taskInfo;
  }

  /** Waits for the edit to pause, and reports the task's own failure if it ended instead. */
  private void waitForEditPaused(UUID taskUUID) throws InterruptedException {
    try {
      waitForTaskPaused(taskUUID);
    } catch (RuntimeException e) {
      clearAbortOrPausePositions();
      throw new AssertionError(describeFailure(waitForTask(taskUUID)), e);
    }
  }

  /** The read replica helm upgrades queued after the primary cluster is saved, as task params. */
  private List<JsonNode> readReplicaHelmUpgrades(TaskInfo taskInfo) {
    List<TaskInfo> subTasks =
        taskInfo.getSubTasks().stream()
            .sorted(Comparator.comparing(TaskInfo::getPosition))
            .collect(Collectors.toList());
    int primaryIntentPosition =
        subTasks.stream()
            .filter(t -> t.getTaskType() == TaskType.UpdateUniverseIntent)
            .map(TaskInfo::getPosition)
            .findFirst()
            .orElseThrow();
    return subTasks.stream()
        .filter(t -> t.getPosition() > primaryIntentPosition)
        .filter(t -> t.getTaskType() == TaskType.KubernetesCommandExecutor)
        .map(TaskInfo::getTaskParams)
        .filter(
            p ->
                HELM_UPGRADE.name().equals(p.path("commandType").asText())
                    && p.path("isReadOnlyCluster").asBoolean())
        .collect(Collectors.toList());
  }

  /** Asserts that the read replica is upgraded with master addresses that include newMasterAz. */
  private void assertReadReplicaGetsNewMasters(TaskInfo taskInfo, String newMasterAz) {
    List<JsonNode> upgrades = readReplicaHelmUpgrades(taskInfo);
    assertFalse("no read replica helm upgrade queued", upgrades.isEmpty());
    for (JsonNode upgrade : upgrades) {
      String masterAddresses = upgrade.path("masterAddresses").asText();
      assertTrue(masterAddresses, masterAddresses.contains(newMasterAz));
    }
  }

  /**
   * Records a failed earlier attempt of the edit that already saved the primary cluster, as the
   * last task of the universe.
   */
  private TaskInfo createFailedEarlierEdit() {
    TaskInfo previousTask = new TaskInfo(TaskType.EditKubernetesUniverse, UUID.randomUUID());
    previousTask.setOwner("test");
    previousTask.setTaskParams(Json.newObject());
    previousTask.setTaskVersion(ITask.DEFAULT_TASK_VERSION);
    previousTask.save();
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            u -> {
              UniverseDefinitionTaskParams details = u.getUniverseDetails();
              details.updatingTask = TaskType.EditKubernetesUniverse;
              details.updatingTaskUUID = previousTask.getUuid();
              details.placementModificationTaskUuid = previousTask.getUuid();
              details.updateInProgress = false;
              details.updateSucceeded = false;
              u.setUniverseDetails(details);
            });
    return previousTask;
  }

  private void verifyRetryAfterPrimarySavedUpdatesReadReplica(boolean includeReadReplica)
      throws InterruptedException {
    setupUniverseMultiAZ(/* Create Masters */ true, 3);
    addReadReplicaToMultiAZUniverse();
    factory.globalRuntimeConf().setValue("yb.checks.leaderless_tablets.enabled", "false");
    Region region = Region.getByCode(kubernetesProvider, "region-1");
    AvailabilityZone az3 = AvailabilityZone.getByCode(kubernetesProvider, "az-3");
    AvailabilityZone az4 =
        AvailabilityZone.createOrThrow(region, "az-4", "PlacementAZ 4", "subnet-4");
    // Replace az-3 with az-4, which moves a master.
    PlacementInfo pi = defaultUniverse.getUniverseDetails().getPrimaryCluster().placementInfo;
    pi.cloudList.get(0).regionList.get(0).azList.removeIf(az -> az.uuid.equals(az3.getUuid()));
    PlacementInfoUtil.addPlacementZone(az4.getUuid(), pi, 1, 1, true);

    TaskInfo firstAttempt = queueAndAbortEdit(editParams(pi, includeReadReplica));
    assertTrue(firstAttempt.getRuntimeInfo().path("masterAddressesChanged").asBoolean());
    assertReadReplicaGetsNewMasters(firstAttempt, "az-4");

    // The first attempt saved the primary cluster before it failed.
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            u -> {
              UniverseDefinitionTaskParams details = u.getUniverseDetails();
              details.getPrimaryCluster().placementInfo = pi;
              u.setUniverseDetails(details);
            });
    // Retry with the stored params, as CustomerTaskManager#retryCustomerTask does.
    UniverseDefinitionTaskParams retryParams =
        Json.fromJson(firstAttempt.getTaskParams(), UniverseDefinitionTaskParams.class);
    retryParams.setPreviousTaskUUID(firstAttempt.getUuid());
    TaskInfo retry = queueAndAbortEdit(retryParams);

    assertReadReplicaGetsNewMasters(retry, "az-4");
  }

  @Test
  public void testRetryAfterPrimarySavedUpdatesReadReplicaMasterAddresses()
      throws InterruptedException {
    verifyRetryAfterPrimarySavedUpdatesReadReplica(true /* includeReadReplica */);
  }

  @Test
  public void testPrimaryOnlyRetryAfterPrimarySavedUpdatesReadReplicaMasterAddresses()
      throws InterruptedException {
    verifyRetryAfterPrimarySavedUpdatesReadReplica(false /* includeReadReplica */);
  }

  @Test
  public void testRetryWithoutMasterAddressChangeLeavesReadReplica() throws InterruptedException {
    setupUniverseMultiAZ(/* Create Masters */ true, 3);
    addReadReplicaToMultiAZUniverse();
    factory.globalRuntimeConf().setValue("yb.checks.leaderless_tablets.enabled", "false");
    TaskInfo earlierEdit = createFailedEarlierEdit();
    UniverseDefinitionTaskParams taskParams =
        editParams(
            defaultUniverse.getUniverseDetails().getPrimaryCluster().placementInfo,
            true /* includeReadReplica */);
    taskParams.setPreviousTaskUUID(earlierEdit.getUuid());

    TaskInfo taskInfo = queueAndAbortEdit(taskParams);

    assertTrue(readReplicaHelmUpgrades(taskInfo).isEmpty());
  }
}
