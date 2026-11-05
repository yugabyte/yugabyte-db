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
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.ArgumentMatchers.contains;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.ArgumentMatchers.isNull;
import static org.mockito.Mockito.atLeastOnce;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.doNothing;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.common.collect.ImmutableSet;
import com.google.common.collect.Sets;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.DeltaEvaluator;
import com.yugabyte.yw.common.KubernetesUtil;
import com.yugabyte.yw.common.NodeDetailsArrayComparator;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.ProviderInitializer;
import com.yugabyte.yw.common.RegexMatcher;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.rollback.EditKubernetesUniverseRollbackComputer;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent.K8SNodeResourceSpec;
import com.yugabyte.yw.metrics.MetricQueryResponse;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.PlacementInfo.PlacementAZ;
import com.yugabyte.yw.models.helpers.StateTransitionDetails;
import com.yugabyte.yw.models.helpers.TaskType;
import io.fabric8.kubernetes.api.model.Pod;
import io.fabric8.kubernetes.api.model.PodList;
import io.fabric8.kubernetes.api.model.storage.StorageClass;
import io.fabric8.kubernetes.client.utils.Serialization;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.function.Predicate;
import java.util.stream.Collectors;
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
import org.yb.client.ListMasterRaftPeersResponse;
import org.yb.client.YBClientApi;
import org.yb.util.PeerInfo;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
@Slf4j
public class EditKubernetesUniverseTest extends CommissionerBaseTest {

  private EditKubernetesUniverse editUniverse;

  private Universe defaultUniverse;
  private YBClientApi mockClient;

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
    mockClient = mock(YBClientApi.class);
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
    // WaitForServer(YSQLSERVER) calls ysqlQueryExecutor.executeQueryInNodeShell; stub success.
    when(mockYsqlQueryExecutor.executeQueryInNodeShell(
            any(), any(), any(), anyBoolean(), anyBoolean()))
        .thenReturn(Json.newObject());
    when(mockYBClient.getUniverseClient(any())).thenReturn(mockClient);
    when(mockYBClient.getClient(any(), any())).thenReturn(mockClient);
    factory.globalRuntimeConf().setValue("yb.checks.leaderless_tablets.enabled", "true");
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
    ApiUtils.configureDedicatedMasterFields(userIntent);
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
          TaskType.MarkRollbackUnsafe,
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
          TaskType.MarkRollbackUnsafe,
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
          TaskType.MarkRollbackUnsafe,
          TaskType.UpdatePlacementInfo,
          TaskType.HandleKubernetesNamespacedServices,
          TaskType.CheckUnderReplicatedTablets,
          TaskType.CheckNodesAreSafeToTakeDown,
          TaskType.KubernetesCommandExecutor,
          TaskType.KubernetesWaitForPod,
          TaskType.WaitForServer,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.CheckFollowerLag,
          TaskType.CheckUnderReplicatedTablets,
          TaskType.CheckNodesAreSafeToTakeDown,
          TaskType.KubernetesCommandExecutor,
          TaskType.KubernetesWaitForPod,
          TaskType.WaitForServer,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.CheckFollowerLag,
          TaskType.CheckUnderReplicatedTablets,
          TaskType.CheckNodesAreSafeToTakeDown,
          TaskType.KubernetesCommandExecutor,
          TaskType.KubernetesWaitForPod,
          TaskType.WaitForServer,
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
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of("commandType", HELM_UPGRADE.name())),
        Json.toJson(ImmutableMap.of("commandType", WAIT_FOR_POD.name())),
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
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of("commandType", HELM_UPGRADE.name())),
        Json.toJson(ImmutableMap.of("commandType", WAIT_FOR_POD.name())),
        Json.toJson(ImmutableMap.of()),
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
    taskParams.upsertPrimaryCluster(userIntent, null, pi);
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
    factory
        .forUniverse(defaultUniverse)
        .setValue("yb.checks.node_disk_size.target_usage_percentage", "0");
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
            expectedOverrideFile.capture(),
            isNull());
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
    factory
        .forUniverse(defaultUniverse)
        .setValue("yb.checks.node_disk_size.target_usage_percentage", "0");
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
            expectedOverrideFile.capture(),
            isNull());
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
    factory
        .forUniverse(defaultUniverse)
        .setValue("yb.checks.node_disk_size.target_usage_percentage", "0");
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
    TestUtils.updateInstanceType(newUserIntent, "c5.xlarge");
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
            expectedOverrideFile.capture(),
            isNull());
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

  //   @Test
  public void testEditKubernetesUniverseRetry() {
    setupUniverseSingleAZ(/* Create Masters */ true);
    factory
        .forUniverse(defaultUniverse)
        .setValue("yb.checks.node_disk_size.target_usage_percentage", "0");
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
    taskParams.upsertPrimaryCluster(newUserIntent, null, pi);
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
    TestUtils.existingProviderInitializer(taskParams.clusters.get(0).userIntent)
        .updateDeviceInfo(
            deviceInfo -> deviceInfo.volumeSize = 150); /* Changed volume size 100 -> 150 */
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
    factory.globalRuntimeConf().setValue("yb.checks.leaderless_tablets.enabled", "false");
    factory.globalRuntimeConf().setValue("yb.checks.change_master_config.enabled", "false");
    factory
        .forUniverse(defaultUniverse)
        .setValue("yb.checks.node_disk_size.target_usage_percentage", "100");
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
    factory.globalRuntimeConf().setValue("yb.checks.leaderless_tablets.enabled", "false");
    factory.globalRuntimeConf().setValue("yb.checks.change_master_config.enabled", "false");
    factory
        .forUniverse(defaultUniverse)
        .setValue("yb.checks.node_disk_size.target_usage_percentage", "100");
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
    factory.globalRuntimeConf().setValue("yb.checks.leaderless_tablets.enabled", "false");
    factory.globalRuntimeConf().setValue("yb.checks.change_master_config.enabled", "false");
    factory
        .forUniverse(defaultUniverse)
        .setValue("yb.checks.node_disk_size.target_usage_percentage", "120");
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

    ProviderInitializer providerInitializer =
        TestUtils.getProviderInitializerForTests(userIntent, kubernetesProvider.getUuid());
    providerInitializer.setProviderType(Common.CloudType.kubernetes);
    providerInitializer.setAccessCode("demo-access");
    providerInitializer.setDeviceInfo(ApiUtils.getDummyDeviceInfo(1, 100));

    userIntent.ybSoftwareVersion = "2.28.0.0";
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
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
    TestUtils.updateDeviceInfo(
        taskParams.getReadOnlyClusters().get(0).userIntent,
        ServerType.TSERVER,
        deviceInfo -> deviceInfo.volumeSize--);

    taskParams.nodePrefix = NODE_PREFIX;
    Exception expected = null;
    try {
      UUID taskUUID = commissioner.submit(TaskType.EditKubernetesUniverse, taskParams);
      waitForTask(taskUUID);
    } catch (Exception e) {
      expected = e;
    }
    assertNotNull(expected);
    assertTrue(
        expected.getMessage().contains("Cannot perform full move in this Kubernetes cluster"));
  }

  @Test
  public void testFullMoveNotSupportedFailsValidation() {
    // Universe with old DB version that does not support full move
    setupUniverseSingleAZ(/* setMasters */ true);
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        u -> {
          u.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion = "2.28.0.0";
        });
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());

    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = defaultUniverse.getVersion();
    taskParams.nodePrefix = NODE_PREFIX;
    taskParams.nodeDetailsSet = defaultUniverse.getUniverseDetails().nodeDetailsSet;
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    Cluster primaryCluster = taskParams.getPrimaryCluster();
    UniverseDefinitionTaskParams.UserIntent newUserIntent = primaryCluster.userIntent.clone();
    // Change storage class to trigger full move (not just volume size change)
    TestUtils.updateDeviceInfo(
        newUserIntent, ServerType.TSERVER, deviceInfo -> deviceInfo.storageClass = "fast");
    PlacementInfo pi = defaultUniverse.getUniverseDetails().getPrimaryCluster().placementInfo;
    taskParams.upsertPrimaryCluster(newUserIntent, null, pi);
    taskParams.getPrimaryCluster().uuid = primaryCluster.uuid;

    Exception expected = null;
    try {
      UUID taskUUID = commissioner.submit(TaskType.EditKubernetesUniverse, taskParams);
      waitForTask(taskUUID);
    } catch (Exception e) {
      expected = e;
    }
    assertNotNull(expected);
    assertTrue(
        expected.getMessage().contains("Cannot perform full move in this Kubernetes cluster"));
  }

  @Test
  public void testFullMoveSupportedPassesValidation() {
    // Universe with DB version that supports full move - only volume size increase (no full move
    // needed) to verify we don't block normal edits. validateParams runs synchronously in submit().
    setupUniverseSingleAZ(/* setMasters */ true);
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        u -> {
          u.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion =
              KubernetesUtil.MIN_VERSION_FULL_MOVE_SUPPORT_STABLE;
        });
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());

    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = defaultUniverse.getVersion();
    taskParams.nodePrefix = NODE_PREFIX;
    taskParams.nodeDetailsSet = defaultUniverse.getUniverseDetails().nodeDetailsSet;
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    Cluster primaryCluster = taskParams.getPrimaryCluster();
    UniverseDefinitionTaskParams.UserIntent newUserIntent = primaryCluster.userIntent.clone();
    DeviceInfo deviceInfo =
        newUserIntent.getBaseDeviceInfo(newUserIntent.maybeGetSingleProviderUUID().get());
    // Only increase volume size - does not need full move
    if (deviceInfo.volumeSize != null) {
      deviceInfo.volumeSize = deviceInfo.volumeSize + 10;
    } else {
      deviceInfo.volumeSize = 100;
      deviceInfo.numVolumes = 1;
    }
    PlacementInfo pi = defaultUniverse.getUniverseDetails().getPrimaryCluster().placementInfo;
    taskParams.upsertPrimaryCluster(newUserIntent, null, pi);
    taskParams.getPrimaryCluster().uuid = primaryCluster.uuid;
    // submit() calls validateParams synchronously; should not throw for volume-only increase
    UUID taskUUID = commissioner.submit(TaskType.EditKubernetesUniverse, taskParams);
    assertNotNull(taskUUID);
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

  private EditKubernetesUniverse createTaskWithParams(UniverseDefinitionTaskParams taskParams) {
    EditKubernetesUniverse task =
        new EditKubernetesUniverse(
            mockBaseTaskDependencies,
            mockKubernetesManagerFactory,
            mockOperatorStatusUpdaterFactory,
            mockYbcManager);
    task.initialize(taskParams);
    return task;
  }

  @Test
  public void testValidateStorageClassesOnEditSuccess() {
    setupUniverseSingleAZ(true);
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.nodeDetailsSet = defaultUniverse.getUniverseDetails().nodeDetailsSet;
    UniverseDefinitionTaskParams.UserIntent newUserIntent =
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.clone();
    TestUtils.updateDeviceInfo(
        newUserIntent, ServerType.TSERVER, deviceInfo -> deviceInfo.storageClass = "standard");
    PlacementInfo pi = defaultUniverse.getUniverseDetails().getPrimaryCluster().placementInfo;
    taskParams.upsertPrimaryCluster(newUserIntent, null, pi);
    taskParams.getPrimaryCluster().uuid =
        defaultUniverse.getUniverseDetails().getPrimaryCluster().uuid;

    when(mockKubernetesManager.getStorageClass(any(), eq("standard")))
        .thenReturn(mock(StorageClass.class));

    EditKubernetesUniverse task = createTaskWithParams(taskParams);
    task.validateStorageClassesOnEdit();
  }

  @Test
  public void testValidateStorageClassesOnEditFailure() {
    setupUniverseSingleAZ(true);
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.nodeDetailsSet = defaultUniverse.getUniverseDetails().nodeDetailsSet;
    UniverseDefinitionTaskParams.UserIntent newUserIntent =
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.clone();
    TestUtils.updateDeviceInfo(
        newUserIntent,
        ServerType.TSERVER,
        deviceInfo -> deviceInfo.storageClass = "nonexistent-sc");
    PlacementInfo pi = defaultUniverse.getUniverseDetails().getPrimaryCluster().placementInfo;
    taskParams.upsertPrimaryCluster(newUserIntent, null, pi);
    taskParams.getPrimaryCluster().uuid =
        defaultUniverse.getUniverseDetails().getPrimaryCluster().uuid;

    when(mockKubernetesManager.getStorageClass(any(), eq("nonexistent-sc")))
        .thenThrow(new RuntimeException("storage class not found"));

    EditKubernetesUniverse task = createTaskWithParams(taskParams);
    Exception thrown = null;
    try {
      task.validateStorageClassesOnEdit();
    } catch (RuntimeException e) {
      thrown = e;
    }
    assertNotNull(thrown);
    assertTrue(thrown.getMessage().contains("does not exist"));
    assertTrue(thrown.getMessage().contains("nonexistent-sc"));
    assertTrue(thrown.getMessage().contains("tserver"));
  }

  @Test
  public void testEditKubernetesUniverseIsRollbackable() {
    // @CanRollback wiring: the K8s edit is rollbackable, the rollback task itself is not.
    assertTrue(
        com.yugabyte.yw.commissioner.Commissioner.canTaskTypeRollback(
            TaskType.EditKubernetesUniverse));
    assertFalse(
        com.yugabyte.yw.commissioner.Commissioner.canTaskTypeRollback(
            TaskType.RollbackEditKubernetesUniverse));
  }

  // Builds a pod list of 1 master + numTservers tservers in the single-AZ namespace.
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

  @Test
  public void testRollbackEditKubernetesUniverseHappyPathAfterFailedExpand()
      throws InterruptedException {
    setupUniverseSingleAZ(/* Create Masters */ true);
    factory.globalRuntimeConf().setValue("yb.task.enable_edit_auto_rollback", "false");
    factory.globalRuntimeConf().setValue("yb.task.allow_edit_universe_rollback", "true");
    factory
        .forUniverse(defaultUniverse)
        .setValue("yb.checks.node_disk_size.target_usage_percentage", "0");

    Universe universe = defaultUniverse;
    int tserversBefore = universe.getUniverseDetails().getPrimaryCluster().userIntent.numNodes;

    // Report the target (5-tserver) pod set for the expand attempt.
    when(mockKubernetesManager.getPodInfos(any(), any(), any())).thenReturn(rollbackPods(5));
    // Fail the edit in the safe window (scale-up helm upgrade, before the MarkRollbackUnsafe
    // checkpoint). The freeze already captured the delta with rollbackSafe=true.
    doThrow(new RuntimeException("helm boom"))
        .when(mockKubernetesManager)
        .helmUpgrade(any(), any(), any(), any(), any(), any(), isNull());

    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.setUniverseUUID(universe.getUniverseUUID());
    taskParams.expectedUniverseVersion = 2;
    taskParams.nodeDetailsSet = universe.getUniverseDetails().nodeDetailsSet;
    UniverseDefinitionTaskParams.UserIntent newUserIntent =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.clone();
    newUserIntent.numNodes = 5;
    newUserIntent.tserverK8SNodeResourceSpec = new K8SNodeResourceSpec();
    newUserIntent.masterK8SNodeResourceSpec = new K8SNodeResourceSpec();
    PlacementInfo pi = universe.getUniverseDetails().getPrimaryCluster().placementInfo;
    pi.cloudList.get(0).regionList.get(0).azList.get(0).numNodesInAZ = 5;
    TaskInfo failedEdit = submitTask(taskParams, newUserIntent, pi);
    assertEquals(Failure, failedEdit.getTaskState());

    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertNotNull(universe.getStateTransitionDetails());
    assertTrue(universe.getStateTransitionDetails().isRollbackSafe());
    assertTrue(commissioner.canTaskRollback(failedEdit));
    assertTrue(commissioner.canTaskRollbackDetailed(failedEdit));
    // With the flag off, neither listing nor submit offers rollback of the same failed edit.
    EditKubernetesUniverseRollbackComputer rollbackComputer =
        app.injector().instanceOf(EditKubernetesUniverseRollbackComputer.class);
    assertTrue(rollbackComputer.isEnabled());
    factory.globalRuntimeConf().setValue("yb.task.allow_edit_universe_rollback", "false");
    assertFalse(rollbackComputer.isEnabled());
    assertFalse(commissioner.canTaskRollback(failedEdit));
    assertFalse(commissioner.canTaskRollbackDetailed(failedEdit));
    factory.globalRuntimeConf().setValue("yb.task.allow_edit_universe_rollback", "true");

    // Scale-back-down rollback: helm succeeds, pods report the restored pre-edit set.
    doNothing()
        .when(mockKubernetesManager)
        .helmUpgrade(any(), any(), any(), any(), any(), any(), isNull());
    when(mockKubernetesManager.getPodInfos(any(), any(), any()))
        .thenReturn(rollbackPods(tserversBefore));

    UniverseDefinitionTaskParams rollbackParams =
        Json.fromJson(failedEdit.getTaskParams(), UniverseDefinitionTaskParams.class);
    rollbackParams.setUniverseUUID(universe.getUniverseUUID());
    rollbackParams.expectedUniverseVersion = -1;
    UUID rollbackUuid =
        commissioner.submit(TaskType.RollbackEditKubernetesUniverse, rollbackParams);
    TaskInfo rollbackInfo = waitForTask(rollbackUuid);
    assertEquals(Success, rollbackInfo.getTaskState());

    // Rollback scales the StatefulSet back down (helm) then restores details, in that order.
    List<TaskType> rollbackTypes =
        rollbackInfo.getSubTasks().stream().map(TaskInfo::getTaskType).collect(Collectors.toList());
    int helmIdx = rollbackTypes.indexOf(TaskType.KubernetesCommandExecutor);
    int restoreIdx = rollbackTypes.indexOf(TaskType.RestoreUniverseDetailsFromDelta);
    int successIdx = rollbackTypes.indexOf(TaskType.UniverseUpdateSucceeded);
    assertTrue("expected a helm/kubernetes command", helmIdx >= 0);
    assertTrue("details restored after scale-down", restoreIdx > helmIdx);
    assertTrue("update marked successful last", successIdx > restoreIdx);

    // A scale-down helm upgrade was issued to shrink the StatefulSet back to the pre-edit size.
    verify(mockKubernetesManager, atLeastOnce())
        .helmUpgrade(any(), any(), any(), any(), any(), any(), isNull());

    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertEquals(
        tserversBefore, universe.getUniverseDetails().getPrimaryCluster().userIntent.numNodes);
    assertTrue(universe.getUniverseDetails().updateSucceeded);
    assertNull(universe.getUniverseDetails().placementModificationTaskUuid);
    assertNull(universe.getStateTransitionDetails());
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

  private void enableManualEditRollback() {
    factory.globalRuntimeConf().setValue("yb.task.enable_edit_auto_rollback", "false");
    factory.globalRuntimeConf().setValue("yb.task.allow_edit_universe_rollback", "true");
    factory
        .forUniverse(defaultUniverse)
        .setValue("yb.checks.node_disk_size.target_usage_percentage", "0");
  }

  private TaskInfo submitRollbackEditKubernetesUniverse(TaskInfo failedEdit, Universe universe)
      throws InterruptedException {
    UniverseDefinitionTaskParams rollbackParams =
        Json.fromJson(failedEdit.getTaskParams(), UniverseDefinitionTaskParams.class);
    rollbackParams.setUniverseUUID(universe.getUniverseUUID());
    rollbackParams.expectedUniverseVersion = -1;
    return waitForTask(
        commissioner.submit(TaskType.RollbackEditKubernetesUniverse, rollbackParams));
  }

  /**
   * Adds a one-pod read replica to the single-AZ universe, then fails a read-replica-only edit that
   * adds a pod, in the safe window (before the MarkRollbackUnsafe checkpoint). Like the API, the
   * edit carries only the read replica cluster.
   */
  private TaskInfo failReadReplicaOnlyEdit() throws InterruptedException {
    setupUniverseSingleAZ(/* Create Masters */ true);
    enableManualEditRollback();
    UniverseDefinitionTaskParams.UserIntent rrIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    rrIntent.numNodes = 1;
    rrIntent.replicationFactor = 1;
    ProviderInitializer providerInitializer =
        TestUtils.getProviderInitializerForTests(rrIntent, kubernetesProvider.getUuid());
    providerInitializer.setProviderType(Common.CloudType.kubernetes);
    providerInitializer.setAccessCode("demo-access");
    providerInitializer.setDeviceInfo(ApiUtils.getDummyDeviceInfo(1, 100));
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

    Cluster readReplica = defaultUniverse.getUniverseDetails().getReadOnlyClusters().get(0);
    readReplica.userIntent.numNodes = 2;
    readReplica.placementInfo.cloudList.get(0).regionList.get(0).azList.get(0).numNodesInAZ = 2;
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = defaultUniverse.getVersion();
    taskParams.nodeDetailsSet = defaultUniverse.getUniverseDetails().nodeDetailsSet;
    taskParams.clusters = new ArrayList<>(Collections.singletonList(readReplica));
    taskParams.nodePrefix = NODE_PREFIX;

    when(mockKubernetesManager.getPodInfos(any(), any(), any())).thenReturn(rollbackPods(3));
    doThrow(new RuntimeException("helm boom"))
        .when(mockKubernetesManager)
        .helmUpgrade(any(), any(), any(), any(), any(), any(), isNull());
    TaskInfo failedEdit =
        waitForTask(commissioner.submit(TaskType.EditKubernetesUniverse, taskParams));
    assertEquals(describeFailure(failedEdit), Failure, failedEdit.getTaskState());
    verify(mockKubernetesManager, atLeastOnce())
        .helmUpgrade(any(), any(), any(), any(), any(), any(), isNull());
    return failedEdit;
  }

  /** Rolls back the failed read-replica-only edit and checks the universe is restored. */
  private void rollBackReadReplicaOnlyEdit(TaskInfo failedEdit) throws InterruptedException {
    doNothing()
        .when(mockKubernetesManager)
        .helmUpgrade(any(), any(), any(), any(), any(), any(), isNull());
    // The rollback waits for the pre-edit pod count of each cluster: the primary's masters and
    // tservers, and the read replica's single tserver (its helm release name ends in "-rr").
    when(mockKubernetesManager.getPodInfos(any(), any(), any()))
        .thenAnswer(
            invocation ->
                String.valueOf(invocation.<String>getArgument(1)).contains("-rr")
                    ? tserverPods(1)
                    : rollbackPods(3));
    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    TaskInfo rollbackInfo = submitRollbackEditKubernetesUniverse(failedEdit, universe);
    assertEquals(describeFailure(rollbackInfo), Success, rollbackInfo.getTaskState());

    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertEquals(1, universe.getUniverseDetails().getReadOnlyClusters().get(0).userIntent.numNodes);
    assertTrue(universe.getUniverseDetails().updateSucceeded);
    assertNull(universe.getUniverseDetails().placementModificationTaskUuid);
    assertNull(universe.getStateTransitionDetails());
  }

  @Test
  public void testRollbackEditKubernetesUniverseOfReadReplicaOnlyEdit()
      throws InterruptedException {
    TaskInfo failedEdit = failReadReplicaOnlyEdit();
    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    StateTransitionDetails details = universe.getStateTransitionDetails();
    assertNotNull(details);
    assertTrue(details.isRollbackSafe());
    // The primary is not edited, so the checkpoint keeps it as part of the target.
    assertNotNull(details.getTargetUniverseDetails().getPrimaryCluster());
    assertTrue(commissioner.canTaskRollbackDetailed(failedEdit));

    rollBackReadReplicaOnlyEdit(failedEdit);
  }

  @Test
  public void testRollbackEditKubernetesUniverseOfReadReplicaOnlyEditWithOlderCheckpoint()
      throws InterruptedException {
    TaskInfo failedEdit = failReadReplicaOnlyEdit();
    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    // A checkpoint without the primary cluster, as an older build wrote it. The rollback must use
    // the pre-edit primary instead of crashing after the freeze.
    StateTransitionDetails details = universe.getStateTransitionDetails();
    UniverseDefinitionTaskParams target = details.getTargetUniverseDetails();
    target.clusters.removeIf(
        c -> c.clusterType == UniverseDefinitionTaskParams.ClusterType.PRIMARY);
    JsonNode olderDelta =
        DeltaEvaluator.buildDeltaJsonTree(
            details.getBeforeUniverseDetails(), target, new NodeDetailsArrayComparator());
    Universe.saveDetails(
        universe.getUniverseUUID(),
        u -> u.setStateTransitionDetails(new StateTransitionDetails(true, olderDelta)));
    assertNull(
        Universe.getOrBadRequest(universe.getUniverseUUID())
            .getStateTransitionDetails()
            .getTargetUniverseDetails()
            .getPrimaryCluster());

    rollBackReadReplicaOnlyEdit(failedEdit);
  }

  @Test
  public void testRollbackEditKubernetesUniverseOfZoneReplaceEdit() throws InterruptedException {
    setupUniverseMultiAZ(/* Create Masters */ true, 3);
    enableManualEditRollback();
    factory.globalRuntimeConf().setValue("yb.checks.leaderless_tablets.enabled", "false");
    Universe universe = defaultUniverse;
    Region region = Region.getByCode(kubernetesProvider, "region-1");
    AvailabilityZone az3 = AvailabilityZone.getByCode(kubernetesProvider, "az-3");
    AvailabilityZone az4 =
        AvailabilityZone.createOrThrow(region, "az-4", "PlacementAZ 4", "subnet-4");
    Set<UUID> zonesBefore =
        universe
            .getUniverseDetails()
            .getPrimaryCluster()
            .placementInfo
            .azStream()
            .map(az -> az.uuid)
            .collect(Collectors.toSet());

    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.setUniverseUUID(universe.getUniverseUUID());
    taskParams.expectedUniverseVersion = universe.getVersion();
    taskParams.nodeDetailsSet = universe.getUniverseDetails().nodeDetailsSet;
    UniverseDefinitionTaskParams.UserIntent newUserIntent =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.clone();
    // Replace az-3 with az-4: still RF3 with one master and one tserver per zone.
    PlacementInfo pi = universe.getUniverseDetails().getPrimaryCluster().placementInfo;
    pi.cloudList.get(0).regionList.get(0).azList.removeIf(az -> az.uuid.equals(az3.getUuid()));
    PlacementInfoUtil.addPlacementZone(az4.getUuid(), pi, 1, 1, true);

    // Fail the edit when the new zone's pods start, before the MarkRollbackUnsafe checkpoint.
    doThrow(new RuntimeException("helm boom"))
        .when(mockKubernetesManager)
        .helmInstall(any(), any(), any(), any(), any(), any(), any());
    TaskInfo failedEdit = submitTask(taskParams, newUserIntent, pi);
    assertEquals(describeFailure(failedEdit), Failure, failedEdit.getTaskState());
    verify(mockKubernetesManager, atLeastOnce())
        .helmInstall(any(), any(), any(), any(), any(), any(), any());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertTrue(universe.getStateTransitionDetails().isRollbackSafe());
    assertTrue(commissioner.canTaskRollbackDetailed(failedEdit));

    // The scale-down waits for one master and one tserver in each pre-edit zone.
    when(mockKubernetesManager.getPodInfos(any(), any(), any())).thenReturn(rollbackPods(1));
    TaskInfo rollbackInfo = submitRollbackEditKubernetesUniverse(failedEdit, universe);
    assertEquals(describeFailure(rollbackInfo), Success, rollbackInfo.getTaskState());

    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertEquals(
        zonesBefore,
        universe
            .getUniverseDetails()
            .getPrimaryCluster()
            .placementInfo
            .azStream()
            .map(az -> az.uuid)
            .collect(Collectors.toSet()));
    assertTrue(universe.getUniverseDetails().updateSucceeded);
    assertNull(universe.getUniverseDetails().placementModificationTaskUuid);
    assertNull(universe.getStateTransitionDetails());
  }

  // The tests below check the plan an edit builds rather than its execution. Every subtask is saved
  // with its params when the plan is built, so failing the first helm call leaves the whole plan
  // to inspect: what each helm call renders and where each master address push sits.

  private static final String FULL_MOVE_VERSION = "2026.1.0.0-b1";
  private static final String PRE_NON_RESTART_VERSION = "2024.1.0.0-b1";

  /** RF3 universe with one master and one tserver in each of az-1..az-3. */
  private void setupMasterMoveUniverse(String ybSoftwareVersion) {
    setupUniverseMultiAZ(/* setMasters */ true, 3);
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            u ->
                u.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion =
                    ybSoftwareVersion);
    factory.globalRuntimeConf().setValue("yb.task.enable_edit_auto_rollback", "false");
    factory.globalRuntimeConf().setValue("yb.checks.leaderless_tablets.enabled", "false");
    factory
        .forUniverse(defaultUniverse)
        .setValue("yb.checks.node_disk_size.target_usage_percentage", "0");
    when(mockKubernetesManager.getStorageClass(any(), any())).thenReturn(mock(StorageClass.class));
    doThrow(new RuntimeException("stop after planning"))
        .when(mockKubernetesManager)
        .helmUpgrade(any(), any(), any(), any(), any(), any(), any());
    doThrow(new RuntimeException("stop after planning"))
        .when(mockKubernetesManager)
        .helmInstall(any(), any(), any(), any(), any(), any(), any());
  }

  private UniverseDefinitionTaskParams.UserIntent currentIntent() {
    return defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.clone();
  }

  private PlacementInfo currentPlacement() {
    return defaultUniverse.getUniverseDetails().getPrimaryCluster().placementInfo;
  }

  /** Submits the edit, which fails at its first helm call once its plan is saved. */
  private TaskInfo submitEdit(
      UniverseDefinitionTaskParams.UserIntent userIntent,
      PlacementInfo pi,
      @javax.annotation.Nullable UUID previousTaskUUID) {
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = -1;
    taskParams.nodeDetailsSet = defaultUniverse.getUniverseDetails().nodeDetailsSet;
    taskParams.setPreviousTaskUUID(previousTaskUUID);
    TaskInfo taskInfo = submitTask(taskParams, userIntent, pi);
    assertEquals(Failure, taskInfo.getTaskState());
    return taskInfo;
  }

  /**
   * Retries the edit the way YBA does, with the task params the earlier attempt saved, changed by
   * the given consumer.
   */
  private TaskInfo retryEdit(
      TaskInfo previous, java.util.function.Consumer<UniverseDefinitionTaskParams> change) {
    UniverseDefinitionTaskParams taskParams =
        Json.fromJson(
            TaskInfo.getOrBadRequest(previous.getUuid()).getTaskParams(),
            UniverseDefinitionTaskParams.class);
    taskParams.setPreviousTaskUUID(previous.getUuid());
    change.accept(taskParams);
    try {
      TaskInfo taskInfo =
          waitForTask(commissioner.submit(TaskType.EditKubernetesUniverse, taskParams));
      assertEquals(Failure, taskInfo.getTaskState());
      return taskInfo;
    } catch (InterruptedException e) {
      throw new RuntimeException(e);
    }
  }

  private TaskInfo retryEdit(TaskInfo previous) {
    return retryEdit(previous, p -> {});
  }

  /** Subtasks of the edit in plan order. */
  private static List<TaskInfo> plan(TaskInfo taskInfo) {
    return taskInfo.getSubTasks().stream()
        .sorted(Comparator.comparingInt(TaskInfo::getPosition))
        .collect(Collectors.toList());
  }

  private List<TaskInfo> planEdit(
      UniverseDefinitionTaskParams.UserIntent userIntent, PlacementInfo pi) {
    return plan(submitEdit(userIntent, pi, null));
  }

  private static List<TaskInfo> helmUpgradesInAZs(List<TaskInfo> helm, Set<String> azCodes) {
    return helm.stream()
        .filter(t -> azCodes.contains(t.getTaskParams().path("azCode").asText()))
        .collect(Collectors.toList());
  }

  private static List<TaskInfo> subtasks(
      List<TaskInfo> plan, TaskType type, Predicate<JsonNode> paramsFilter) {
    return plan.stream()
        .filter(t -> t.getTaskType() == type && paramsFilter.test(t.getTaskParams()))
        .collect(Collectors.toList());
  }

  private static List<TaskInfo> helmUpgrades(List<TaskInfo> plan) {
    return subtasks(
        plan,
        TaskType.KubernetesCommandExecutor,
        p -> HELM_UPGRADE.name().equals(p.path("commandType").asText()));
  }

  private static List<TaskInfo> masterAddrPushes(List<TaskInfo> plan) {
    return subtasks(plan, TaskType.SetFlagInMemory, p -> p.path("updateMasterAddrs").asBoolean());
  }

  private static List<TaskInfo> changeMasterConfigs(List<TaskInfo> plan, String opType) {
    return subtasks(
        plan, TaskType.ChangeMasterConfig, p -> opType.equals(p.path("opType").asText()));
  }

  private static Set<String> nodeNames(List<TaskInfo> tasks, ServerType serverType) {
    return tasks.stream()
        .map(TaskInfo::getTaskParams)
        .filter(p -> serverType.name().equals(p.path("serverType").asText()))
        .map(p -> p.path("nodeName").asText())
        .collect(Collectors.toSet());
  }

  private static Set<String> bestEffortNodeNames(List<TaskInfo> pushes) {
    return pushes.stream()
        .map(TaskInfo::getTaskParams)
        .filter(p -> p.path("ignoreErrors").asBoolean())
        .map(p -> p.path("nodeName").asText())
        .collect(Collectors.toSet());
  }

  private static List<TaskInfo> between(List<TaskInfo> tasks, int afterPos, int beforePos) {
    return tasks.stream()
        .filter(t -> t.getPosition() > afterPos && t.getPosition() < beforePos)
        .collect(Collectors.toList());
  }

  private static int firstPosition(List<TaskInfo> tasks) {
    return tasks.stream().mapToInt(TaskInfo::getPosition).min().getAsInt();
  }

  private static int lastPosition(List<TaskInfo> tasks) {
    return tasks.stream().mapToInt(TaskInfo::getPosition).max().getAsInt();
  }

  private static String helmMasterAddresses(TaskInfo helmTask) {
    return helmTask.getTaskParams().path("masterAddresses").asText();
  }

  private static boolean keepsGflagsChecksum(TaskInfo helmTask) {
    return helmTask.getTaskParams().path("usePreviousGflagsChecksum").asBoolean();
  }

  private static JsonNode helmAZ(TaskInfo helmTask) {
    return helmTask
        .getTaskParams()
        .path("placementInfo")
        .path("cloudList")
        .get(0)
        .path("regionList")
        .get(0)
        .path("azList")
        .get(0);
  }

  private String expectedMasterAddresses(PlacementInfo pi) {
    return KubernetesUtil.computeMasterAddresses(
        pi,
        PlacementInfoUtil.getNumMasterPerAZ(pi),
        NODE_PREFIX,
        defaultUniverse.getName(),
        kubernetesProvider,
        defaultUniverse.getUniverseDetails().communicationPorts.masterRpcPort,
        defaultUniverse.getUniverseDetails().useNewHelmNamingStyle);
  }

  private static Set<String> masterHosts(String masterAddresses) {
    return Arrays.stream(masterAddresses.split(","))
        .map(addr -> addr.substring(addr.indexOf("yb-master"), addr.indexOf('.')))
        .collect(Collectors.toSet());
  }

  /** Checks the two pushes around the master quorum change and returns the post-remove push. */
  private List<TaskInfo> assertPushesAroundMasterChange(
      List<TaskInfo> plan, Set<String> expectedMasters, Set<String> expectedTservers) {
    List<TaskInfo> adds = changeMasterConfigs(plan, "AddMaster");
    List<TaskInfo> removes = changeMasterConfigs(plan, "RemoveMaster");
    List<TaskInfo> pushes = masterAddrPushes(plan);
    List<TaskInfo> preRemove = between(pushes, lastPosition(adds), firstPosition(removes));
    // The post-remove push is the groups right after the leader wait: masters, tservers, and the
    // best-effort push to tservers the edit removes.
    int leaderWait =
        firstPosition(
            between(
                subtasks(plan, TaskType.WaitForMasterLeader, p -> true),
                lastPosition(removes),
                Integer.MAX_VALUE));
    List<TaskInfo> postRemove = between(pushes, leaderWait, leaderWait + 4);
    Set<Integer> postRemoveGroups =
        postRemove.stream().map(TaskInfo::getPosition).collect(Collectors.toSet());
    assertTrue(
        postRemoveGroups.toString(),
        postRemoveGroups.equals(ImmutableSet.of(leaderWait + 1, leaderWait + 2))
            || postRemoveGroups.equals(
                ImmutableSet.of(leaderWait + 1, leaderWait + 2, leaderWait + 3)));
    for (List<TaskInfo> push : ImmutableList.of(preRemove, postRemove)) {
      assertEquals(expectedMasters, nodeNames(push, ServerType.MASTER));
      assertEquals(expectedTservers, nodeNames(push, ServerType.TSERVER));
    }
    return postRemove;
  }

  @Test
  public void testFullMoveMasterAddressPropagation() {
    setupMasterMoveUniverse(FULL_MOVE_VERSION);
    UniverseDefinitionTaskParams.UserIntent newIntent = currentIntent();
    newIntent.ybSoftwareVersion = FULL_MOVE_VERSION;
    TestUtils.updateDeviceInfo(newIntent, ServerType.MASTER, d -> d.storageClass = "fast");
    TestUtils.updateDeviceInfo(newIntent, ServerType.TSERVER, d -> d.storageClass = "fast");
    PlacementInfo pi = currentPlacement();
    pi.azStream()
        .forEach(
            az -> {
              az.masterStsIndex = 1;
              az.tsStsIndex = 1;
            });

    List<TaskInfo> plan = planEdit(newIntent, pi);
    String newMasters = expectedMasterAddresses(pi);
    assertEquals(ImmutableSet.of("yb-master-1-0"), masterHosts(newMasters));
    assertEquals(3, newMasters.split(",").length);

    // Pushes go to the new masters and every tserver serving traffic: the old full-move tservers.
    List<TaskInfo> postRemove =
        assertPushesAroundMasterChange(
            plan,
            ImmutableSet.of("yb-master-1-0_az-1", "yb-master-1-0_az-2", "yb-master-1-0_az-3"),
            ImmutableSet.of("yb-tserver-0_az-1", "yb-tserver-0_az-2", "yb-tserver-0_az-3"));

    List<TaskInfo> helm = helmUpgrades(plan);
    List<TaskInfo> masterHelm =
        helm.stream()
            .filter(t -> "MASTER".equals(t.getTaskParams().path("serverType").asText()))
            .collect(Collectors.toList());
    List<TaskInfo> tserverHelm =
        helm.stream()
            .filter(t -> "TSERVER".equals(t.getTaskParams().path("serverType").asText()))
            .collect(Collectors.toList());
    // Master phase, per AZ: create new master pods, delete the old ones, final render.
    assertEquals(9, masterHelm.size());
    List<TaskInfo> createNewMasters = between(masterHelm, 0, firstPosition(postRemove));
    List<TaskInfo> afterMastersJoined =
        between(masterHelm, lastPosition(postRemove), Integer.MAX_VALUE);
    assertEquals(3, createNewMasters.size());
    assertEquals(6, afterMastersJoined.size());
    for (TaskInfo t : createNewMasters) {
      // New masters start in shell mode, and nothing in the release changes yet.
      assertEquals("", helmMasterAddresses(t));
      assertFalse(keepsGflagsChecksum(t));
    }
    for (TaskInfo t : afterMastersJoined) {
      // The pod delete and final render rewrite every gflags Secret in the release, tservers
      // included, without rolling any pod.
      assertEquals(newMasters, helmMasterAddresses(t));
      assertTrue(keepsGflagsChecksum(t));
      assertEquals(1, t.getTaskParams().path("tserverPartition").asInt());
    }
    // Tserver phase renders the new list, as before.
    assertFalse(tserverHelm.isEmpty());
    assertTrue(firstPosition(tserverHelm) > lastPosition(masterHelm));
    for (TaskInfo t : tserverHelm) {
      assertEquals(newMasters, helmMasterAddresses(t));
      assertFalse(keepsGflagsChecksum(t));
    }
    // The first master pod delete comes after the post-remove push.
    List<TaskInfo> podDeletes = subtasks(plan, TaskType.CheckNodeSafeToDelete, p -> true);
    assertTrue(firstPosition(podDeletes) > lastPosition(postRemove));
  }

  @Test
  public void testZoneReplaceMasterAddressPropagation() {
    setupMasterMoveUniverse(FULL_MOVE_VERSION);
    AvailabilityZone az1 = AvailabilityZone.getByCode(kubernetesProvider, "az-1");
    AvailabilityZone az2 = AvailabilityZone.getByCode(kubernetesProvider, "az-2");
    PlacementInfo pi = zoneReplacePlacement();

    List<TaskInfo> plan = planEdit(currentIntent(), pi);
    String newMasters = expectedMasterAddresses(pi);

    // The leaving az-3 tserver still serves traffic until its data moves, so it gets the pushes.
    // It may be down, so a failed push to it does not fail the edit.
    List<TaskInfo> postRemove =
        assertPushesAroundMasterChange(
            plan,
            ImmutableSet.of("yb-master-0_az-1", "yb-master-0_az-2", "yb-master-0_az-4"),
            ImmutableSet.of("yb-tserver-0_az-1", "yb-tserver-0_az-2", "yb-tserver-0_az-3"));
    assertEquals(ImmutableSet.of("yb-tserver-0_az-3"), bestEffortNodeNames(masterAddrPushes(plan)));
    assertEquals(
        2,
        masterAddrPushes(plan).stream()
            .filter(t -> t.getTaskParams().path("ignoreErrors").asBoolean())
            .count());
    assertEquals(
        ImmutableList.of("AddMaster"),
        changeMasterConfigs(plan, "AddMaster").stream()
            .map(t -> t.getTaskParams().path("opType").asText())
            .collect(Collectors.toList()));

    // Between the pre-remove push and the remove, a non-restart render rewrites the Secrets of
    // every AZ but the one gaining a master, at their current counts. Nothing scales. The leaving
    // az-3 tserver gets the new list too, so if it restarts it does not depend on removed masters.
    List<TaskInfo> removes = changeMasterConfigs(plan, "RemoveMaster");
    List<TaskInfo> preRemove = between(masterAddrPushes(plan), 0, firstPosition(removes));
    List<TaskInfo> earlySecretUpdate = earlySecretUpdate(plan);
    assertEquals(ImmutableSet.of("az-1", "az-2", "az-3"), azCodes(earlySecretUpdate));
    assertEquals(3, earlySecretUpdate.size());
    assertEquals(lastPosition(preRemove) + 1, firstPosition(earlySecretUpdate));
    assertEquals(firstPosition(earlySecretUpdate) + 2, firstPosition(removes));
    for (TaskInfo t : earlySecretUpdate) {
      assertEquals(newMasters, helmMasterAddresses(t));
      assertTrue(keepsGflagsChecksum(t));
      JsonNode az = helmAZ(t);
      assertEquals(1, az.path("replicationFactor").asInt());
      assertEquals(1, az.path("numNodesInAZ").asInt());
      assertEquals(1, t.getTaskParams().path("masterPartition").asInt());
      assertEquals(1, t.getTaskParams().path("tserverPartition").asInt());
    }
    // Every helm render after the master move carries the new list.
    for (TaskInfo t : between(helmUpgrades(plan), lastPosition(postRemove), Integer.MAX_VALUE)) {
      assertEquals(newMasters, helmMasterAddresses(t));
    }
  }

  @Test
  public void testZoneReplaceWithoutNonRestartSupportSkipsEarlySecretUpdate() {
    setupMasterMoveUniverse(PRE_NON_RESTART_VERSION);
    PlacementInfo pi = zoneReplacePlacement();

    List<TaskInfo> plan = planEdit(currentIntent(), pi);

    // The in-memory pushes do not depend on non-restart helm support.
    List<TaskInfo> postRemove =
        assertPushesAroundMasterChange(
            plan,
            ImmutableSet.of("yb-master-0_az-1", "yb-master-0_az-2", "yb-master-0_az-4"),
            ImmutableSet.of("yb-tserver-0_az-1", "yb-tserver-0_az-2", "yb-tserver-0_az-3"));
    // This version rolls the pods to change master addresses, so no render before the data move.
    int dataMove = firstPosition(subtasks(plan, TaskType.WaitForDataMove, p -> true));
    assertTrue(
        helmUpgradesInAZs(
                between(
                    helmUpgrades(plan),
                    lastPosition(changeMasterConfigs(plan, "AddMaster")),
                    dataMove),
                ImmutableSet.of("az-1", "az-2", "az-3"))
            .isEmpty());
  }

  @Test
  public void testZoneReplaceRetryBeforePropagationRepeatsIt() {
    setupMasterMoveUniverse(FULL_MOVE_VERSION);
    PlacementInfo pi = zoneReplacePlacement();

    // The first attempt stops before the master move, so the retry plans it all again.
    TaskInfo firstAttempt = submitEdit(currentIntent(), pi, null);
    List<TaskInfo> plan = plan(retryEdit(firstAttempt));

    List<TaskInfo> postRemove =
        assertPushesAroundMasterChange(
            plan,
            ImmutableSet.of("yb-master-0_az-1", "yb-master-0_az-2", "yb-master-0_az-4"),
            ImmutableSet.of("yb-tserver-0_az-1", "yb-tserver-0_az-2", "yb-tserver-0_az-3"));
    assertEquals(ImmutableSet.of("az-1", "az-2", "az-3"), azCodes(earlySecretUpdate(plan)));
    assertEquals(1, subtasks(plan, TaskType.UpdateParentTaskParams, p -> true).size());
  }

  @Test
  public void testRetryOfParamsWithoutPropagationRecordSkipsIt() {
    setupMasterMoveUniverse(FULL_MOVE_VERSION);
    PlacementInfo pi = zoneReplacePlacement();

    // The first attempt records that it has not propagated the new masters yet.
    TaskInfo firstAttempt = submitEdit(currentIntent(), pi, null);
    UniverseDefinitionTaskParams saved =
        Json.fromJson(
            TaskInfo.getOrBadRequest(firstAttempt.getUuid()).getTaskParams(),
            UniverseDefinitionTaskParams.class);
    assertEquals(Boolean.FALSE, saved.getPrimaryCluster().getMasterAddrsPropagated());

    // Params saved by a YBA build without the record may come from an attempt that already
    // changed tserver pods, so the retry neither pushes nor renders.
    List<TaskInfo> plan =
        plan(retryEdit(firstAttempt, p -> p.getPrimaryCluster().setMasterAddrsPropagated(null)));
    assertFalse(changeMasterConfigs(plan, "AddMaster").isEmpty());
    int dataMove = firstPosition(subtasks(plan, TaskType.WaitForDataMove, p -> true));
    assertTrue(between(masterAddrPushes(plan), 0, dataMove).isEmpty());
    assertTrue(earlySecretUpdate(plan).isEmpty());
    assertTrue(subtasks(plan, TaskType.UpdateParentTaskParams, p -> true).isEmpty());
  }

  @Test
  public void testMasterFullMoveRetryOfParamsWithoutPropagationRecordSkipsIt() {
    setupMasterMoveUniverse(FULL_MOVE_VERSION);
    UniverseDefinitionTaskParams.UserIntent newIntent = currentIntent();
    TestUtils.updateDeviceInfo(newIntent, ServerType.MASTER, d -> d.storageClass = "fast");
    PlacementInfo pi = currentPlacement();
    pi.azStream().forEach(az -> az.masterStsIndex = 1);

    TaskInfo firstAttempt = submitEdit(newIntent, pi, null);
    assertFalse(masterAddrPushes(plan(firstAttempt)).isEmpty());
    // The first attempt stopped before the master full move, so the retry plans it again, but
    // without the record it neither pushes nor renders around the master change.
    List<TaskInfo> plan =
        plan(retryEdit(firstAttempt, p -> p.getPrimaryCluster().setMasterAddrsPropagated(null)));
    List<TaskInfo> adds = changeMasterConfigs(plan, "AddMaster");
    List<TaskInfo> removes = changeMasterConfigs(plan, "RemoveMaster");
    assertFalse(removes.isEmpty());
    assertTrue(
        between(masterAddrPushes(plan), firstPosition(adds), lastPosition(removes) + 3).isEmpty());
    assertTrue(earlySecretUpdate(plan).isEmpty());
  }

  @Test
  public void testZoneReplaceWithDiskResizeKeepsLeavingZoneDisks() {
    setupMasterMoveUniverse(FULL_MOVE_VERSION);
    UniverseDefinitionTaskParams.UserIntent newIntent = currentIntent();
    int tserverSize = newIntent.deviceInfo.volumeSize;
    TestUtils.updateDeviceInfo(newIntent, ServerType.TSERVER, d -> d.volumeSize = tserverSize + 10);

    List<TaskInfo> plan = planEdit(newIntent, zoneReplacePlacement());

    // az-1 and az-2 are resized before the master move and render the new size. az-3 leaves the
    // placement, so it is not resized and keeps rendering the size its volumes have.
    List<TaskInfo> earlySecretUpdate = earlySecretUpdate(plan);
    assertEquals(ImmutableSet.of("az-1", "az-2", "az-3"), azCodes(earlySecretUpdate));
    for (TaskInfo t : earlySecretUpdate) {
      JsonNode params = t.getTaskParams();
      boolean leaving = params.path("azCode").asText().equals("az-3");
      assertEquals(!leaving, params.path("useNewTserverDeviceInfo").asBoolean());
      assertEquals(!leaving, params.path("useNewMasterDeviceInfo").asBoolean());
      if (leaving) {
        assertEquals(tserverSize, params.path("oldTsDiskSize").asInt());
      } else {
        assertFalse(params.hasNonNull("oldTsDiskSize"));
      }
    }
  }

  @Test
  public void testZoneReplaceWithStorageClassChangeKeepsCurrentDisks() {
    setupMasterMoveUniverse(FULL_MOVE_VERSION);
    UniverseDefinitionTaskParams.UserIntent newIntent = currentIntent();
    TestUtils.updateDeviceInfo(newIntent, ServerType.TSERVER, d -> d.storageClass = "fast");
    PlacementInfo pi = zoneReplacePlacement();
    pi.azStream().forEach(az -> az.tsStsIndex = 1);

    List<TaskInfo> plan = planEdit(newIntent, pi);

    // az-1 and az-2 have not moved their tservers to the new storage class yet, and az-3 leaves
    // the placement, so all three render the StatefulSets and storage class they run with.
    List<TaskInfo> earlySecretUpdate = earlySecretUpdate(plan);
    assertEquals(ImmutableSet.of("az-1", "az-2", "az-3"), azCodes(earlySecretUpdate));
    for (TaskInfo t : earlySecretUpdate) {
      JsonNode params = t.getTaskParams();
      assertFalse(params.path("useNewTserverDeviceInfo").asBoolean());
      assertFalse(params.path("useNewMasterDeviceInfo").asBoolean());
      assertFalse(params.hasNonNull("oldTsDiskSize"));
      assertEquals(0, helmAZ(t).path("tsStsIndex").asInt());
    }
  }

  @Test
  public void testZoneReplaceWithMasterResizeAndTserverFullMoveKeepsTserverDisks() {
    setupMasterMoveUniverse(FULL_MOVE_VERSION);
    UniverseDefinitionTaskParams.UserIntent newIntent = currentIntent();
    int tserverSize = newIntent.deviceInfo.volumeSize;
    assertNotNull(newIntent.masterDeviceInfo);
    int masterSize = newIntent.masterDeviceInfo.volumeSize;
    TestUtils.updateDeviceInfo(newIntent, ServerType.TSERVER, d -> d.storageClass = "fast");
    TestUtils.updateDeviceInfo(newIntent, ServerType.MASTER, d -> d.volumeSize = masterSize + 10);
    PlacementInfo pi = zoneReplacePlacement();
    pi.azStream().forEach(az -> az.tsStsIndex = 1);

    List<TaskInfo> plan = planEdit(newIntent, pi);

    // The masters of az-1 and az-2 are resized before the master move, so they render the saved,
    // new master size, and only their old tserver StatefulSets keep their size. az-3 leaves the
    // placement unresized and keeps both.
    List<TaskInfo> earlySecretUpdate = earlySecretUpdate(plan);
    assertEquals(ImmutableSet.of("az-1", "az-2", "az-3"), azCodes(earlySecretUpdate));
    for (TaskInfo t : earlySecretUpdate) {
      JsonNode params = t.getTaskParams();
      assertFalse(params.path("useNewTserverDeviceInfo").asBoolean());
      assertFalse(params.path("useNewMasterDeviceInfo").asBoolean());
      assertEquals(tserverSize, params.path("oldTsDiskSize").asInt());
      if (params.path("azCode").asText().equals("az-3")) {
        assertEquals(masterSize, params.path("oldMasterDiskSize").asInt());
      } else {
        assertFalse(params.hasNonNull("oldMasterDiskSize"));
      }
    }
  }

  /** With more AZs than RF, an AZ can keep tservers and no master. */
  @Test
  public void testZoneLosingItsMasterInTserverFullMoveGetsNewMasters() {
    setupMasterMoveUniverse(FULL_MOVE_VERSION);
    UniverseDefinitionTaskParams.UserIntent newIntent = currentIntent();
    TestUtils.updateDeviceInfo(newIntent, ServerType.TSERVER, d -> d.storageClass = "fast");
    AvailabilityZone az4 =
        AvailabilityZone.createOrThrow(
            Region.getByCode(kubernetesProvider, "region-1"), "az-4", "PlacementAZ 4", "subnet-4");
    PlacementInfo pi = currentPlacement();
    pi.azStream().forEach(az -> az.tsStsIndex = 1);
    PlacementInfoUtil.addPlacementZone(az4.getUuid(), pi, 1, 1, true);
    // Masters go to the first AZs in order, so listing az-1 last leaves it without one.
    List<PlacementAZ> azs = pi.cloudList.get(0).regionList.get(0).azList;
    PlacementAZ az1 = azs.remove(0);
    az1.replicationFactor = 0;
    azs.add(az1);
    String newMasters = expectedMasterAddresses(pi);

    List<TaskInfo> plan = planEdit(newIntent, pi);

    assertEquals(
        ImmutableList.of("yb-master-0_az-4"),
        changeMasterConfigs(plan, "AddMaster").stream()
            .map(t -> t.getTaskParams().path("nodeName").asText())
            .collect(Collectors.toList()));
    assertEquals(
        ImmutableList.of("yb-master-0_az-1"),
        changeMasterConfigs(plan, "RemoveMaster").stream()
            .map(t -> t.getTaskParams().path("nodeName").asText())
            .collect(Collectors.toList()));
    // az-1 ends with tservers only, still on their old StatefulSet: its Secret gets the new
    // masters before its own master is removed.
    List<TaskInfo> earlySecretUpdate = earlySecretUpdate(plan);
    assertEquals(ImmutableSet.of("az-1", "az-2", "az-3"), azCodes(earlySecretUpdate));
    TaskInfo az1Render =
        earlySecretUpdate.stream()
            .filter(t -> t.getTaskParams().path("azCode").asText().equals("az-1"))
            .findFirst()
            .get();
    assertEquals(newMasters, helmMasterAddresses(az1Render));
    assertEquals(0, helmAZ(az1Render).path("tsStsIndex").asInt());
    assertEquals(1, helmAZ(az1Render).path("replicationFactor").asInt());
    assertFalse(az1Render.getTaskParams().path("useNewTserverDeviceInfo").asBoolean());
  }

  @Test
  public void testTserverScaleOutDoesNotTouchMasterAddresses() {
    setupMasterMoveUniverse(FULL_MOVE_VERSION);
    PlacementInfo pi = currentPlacement();
    pi.azStream().forEach(az -> az.numNodesInAZ = 2);
    UniverseDefinitionTaskParams.UserIntent newIntent = currentIntent();
    newIntent.numNodes = 6;

    List<TaskInfo> plan = planEdit(newIntent, pi);

    assertTrue(changeMasterConfigs(plan, "AddMaster").isEmpty());
    assertTrue(changeMasterConfigs(plan, "RemoveMaster").isEmpty());
    assertTrue(masterAddrPushes(plan).isEmpty());
    String masters = expectedMasterAddresses(pi);
    List<TaskInfo> helm = helmUpgrades(plan);
    assertFalse(helm.isEmpty());
    for (TaskInfo t : helm) {
      assertEquals(masters, helmMasterAddresses(t));
      // Scale-out renders target counts with partitions that protect the existing pods.
      assertEquals(1, t.getTaskParams().path("masterPartition").asInt());
      assertEquals(1, t.getTaskParams().path("tserverPartition").asInt());
      assertEquals(2, helmAZ(t).path("numNodesInAZ").asInt());
    }
  }

  @Test
  public void testAddTserverOnlyZoneDoesNotTouchMasterAddresses() {
    setupMasterMoveUniverse(FULL_MOVE_VERSION);
    AvailabilityZone az4 =
        AvailabilityZone.createOrThrow(
            Region.getByCode(kubernetesProvider, "region-1"), "az-4", "PlacementAZ 4", "subnet-4");
    PlacementInfo pi = currentPlacement();
    PlacementInfoUtil.addPlacementZone(az4.getUuid(), pi, 0, 1, false);
    UniverseDefinitionTaskParams.UserIntent newIntent = currentIntent();
    newIntent.numNodes = 4;

    List<TaskInfo> plan = planEdit(newIntent, pi);

    assertTrue(changeMasterConfigs(plan, "AddMaster").isEmpty());
    assertTrue(masterAddrPushes(plan).isEmpty());
  }

  /** Replaces az-3 with a new az-4 in the current placement. */
  private PlacementInfo zoneReplacePlacement() {
    AvailabilityZone az3 = AvailabilityZone.getByCode(kubernetesProvider, "az-3");
    AvailabilityZone az4 =
        AvailabilityZone.createOrThrow(
            Region.getByCode(kubernetesProvider, "region-1"), "az-4", "PlacementAZ 4", "subnet-4");
    PlacementInfo pi = currentPlacement();
    pi.cloudList.get(0).regionList.get(0).azList.removeIf(az -> az.uuid.equals(az3.getUuid()));
    PlacementInfoUtil.addPlacementZone(az4.getUuid(), pi, 1, 1, true);
    return pi;
  }

  /** The early Secret update of a plan with a single master step. */
  private static List<TaskInfo> earlySecretUpdate(List<TaskInfo> plan) {
    List<TaskInfo> adds = changeMasterConfigs(plan, "AddMaster");
    List<TaskInfo> removes = changeMasterConfigs(plan, "RemoveMaster");
    return earlySecretUpdate(plan, lastPosition(adds), firstPosition(removes));
  }

  /**
   * The early Secret update: a non-restart helm group between the given positions that waits for
   * the Secret to sync.
   */
  private static List<TaskInfo> earlySecretUpdate(List<TaskInfo> plan, int after, int before) {
    Map<Integer, TaskType> typeAt =
        plan.stream()
            .collect(Collectors.toMap(TaskInfo::getPosition, TaskInfo::getTaskType, (x, y) -> x));
    return helmUpgrades(plan).stream()
        .filter(t -> t.getPosition() > after && t.getPosition() < before)
        .filter(EditKubernetesUniverseTest::keepsGflagsChecksum)
        .filter(t -> typeAt.get(t.getPosition() + 1) == TaskType.WaitForDuration)
        .collect(Collectors.groupingBy(TaskInfo::getPosition))
        .entrySet()
        .stream()
        .min(Map.Entry.comparingByKey())
        .map(Map.Entry::getValue)
        .orElse(Collections.emptyList());
  }

  private static Set<String> azCodes(List<TaskInfo> helmTasks) {
    return helmTasks.stream()
        .map(t -> t.getTaskParams().path("azCode").asText())
        .collect(Collectors.toSet());
  }

  @Test
  public void testZoneReplaceScaledZoneEarlySecretUpdate() {
    setupMasterMoveUniverse(FULL_MOVE_VERSION);
    PlacementInfo pi = zoneReplacePlacement();
    pi.azStream().filter(az -> az.name.equals("PlacementAZ 1")).forEach(az -> az.numNodesInAZ = 2);
    UniverseDefinitionTaskParams.UserIntent newIntent = currentIntent();
    newIntent.numNodes = 4;

    Set<String> masters =
        ImmutableSet.of("yb-master-0_az-1", "yb-master-0_az-2", "yb-master-0_az-4");

    // az-1 renders at its current single tserver, so nothing scales early. A retry that gets
    // here has not scaled az-1 yet, so it renders the same.
    TaskInfo firstAttempt = submitEdit(newIntent, pi, null);
    for (TaskInfo attempt : ImmutableList.of(firstAttempt, retryEdit(firstAttempt))) {
      List<TaskInfo> plan = plan(attempt);
      List<TaskInfo> postRemove =
          assertPushesAroundMasterChange(
              plan,
              masters,
              ImmutableSet.of("yb-tserver-0_az-1", "yb-tserver-0_az-2", "yb-tserver-0_az-3"));
      List<TaskInfo> earlySecretUpdate = earlySecretUpdate(plan);
      assertEquals(ImmutableSet.of("az-1", "az-2", "az-3"), azCodes(earlySecretUpdate));
      for (TaskInfo t : earlySecretUpdate) {
        assertEquals(1, helmAZ(t).path("numNodesInAZ").asInt());
        assertEquals(1, t.getTaskParams().path("tserverPartition").asInt());
      }
    }
  }

  @Test
  public void testZoneGainingMasterSkippedInEarlySecretUpdate() {
    setupMasterMoveUniverse(FULL_MOVE_VERSION);
    AvailabilityZone az3 = AvailabilityZone.getByCode(kubernetesProvider, "az-3");
    PlacementInfo pi = currentPlacement();
    pi.cloudList.get(0).regionList.get(0).azList.removeIf(az -> az.uuid.equals(az3.getUuid()));
    pi.azStream()
        .filter(az -> az.name.equals("PlacementAZ 1"))
        .forEach(
            az -> {
              az.numNodesInAZ = 2;
              az.replicationFactor = 2;
            });
    assertEquals(
        ImmutableSet.of("yb-master-0", "yb-master-1"), masterHosts(expectedMasterAddresses(pi)));

    List<TaskInfo> plan = planEdit(currentIntent(), pi);

    List<TaskInfo> postRemove =
        assertPushesAroundMasterChange(
            plan,
            ImmutableSet.of("yb-master-0_az-1", "yb-master-1_az-1", "yb-master-0_az-2"),
            ImmutableSet.of("yb-tserver-0_az-1", "yb-tserver-0_az-2", "yb-tserver-0_az-3"));
    // az-1 already runs its second master, which a render at the current counts would delete.
    assertEquals(ImmutableSet.of("az-2", "az-3"), azCodes(earlySecretUpdate(plan)));
  }

  @Test
  public void testPartialMasterFullMoveRendersOtherZones() {
    setupMasterMoveUniverse(FULL_MOVE_VERSION);
    UniverseDefinitionTaskParams.UserIntent newIntent = currentIntent();
    TestUtils.updateDeviceInfo(newIntent, ServerType.MASTER, d -> d.storageClass = "fast");
    PlacementInfo pi = currentPlacement();
    pi.azStream()
        .filter(az -> az.name.equals("PlacementAZ 1"))
        .forEach(az -> az.masterStsIndex = 1);

    List<TaskInfo> plan = planEdit(newIntent, pi);
    String newMasters = expectedMasterAddresses(pi);

    List<TaskInfo> postRemove =
        assertPushesAroundMasterChange(
            plan,
            ImmutableSet.of("yb-master-1-0_az-1", "yb-master-0_az-2", "yb-master-0_az-3"),
            ImmutableSet.of("yb-tserver-0_az-1", "yb-tserver-0_az-2", "yb-tserver-0_az-3"));
    // The full move rewrites only az-1's release, so az-2 and az-3 get the new list before the
    // old az-1 master is removed instead of after the data move.
    List<TaskInfo> earlySecretUpdate = earlySecretUpdate(plan);
    assertEquals(ImmutableSet.of("az-2", "az-3"), azCodes(earlySecretUpdate));
    for (TaskInfo t : earlySecretUpdate) {
      assertEquals(newMasters, helmMasterAddresses(t));
      assertEquals(1, helmAZ(t).path("numNodesInAZ").asInt());
      assertEquals(1, t.getTaskParams().path("masterPartition").asInt());
      assertEquals(1, t.getTaskParams().path("tserverPartition").asInt());
    }
    // Before the change, the first render of az-2 and az-3 after the master add was the one at
    // the end of the task.
    assertEquals(
        firstPosition(earlySecretUpdate),
        firstPosition(
            helmUpgradesInAZs(
                between(
                    helmUpgrades(plan),
                    lastPosition(changeMasterConfigs(plan, "AddMaster")),
                    Integer.MAX_VALUE),
                ImmutableSet.of("az-2", "az-3"))));
  }

  @Test
  public void testZoneReplaceWithMasterFullMoveWaitsForAllMasters() {
    setupMasterMoveUniverse(FULL_MOVE_VERSION);
    UniverseDefinitionTaskParams.UserIntent newIntent = currentIntent();
    TestUtils.updateDeviceInfo(newIntent, ServerType.MASTER, d -> d.storageClass = "fast");
    PlacementInfo pi = zoneReplacePlacement();
    pi.azStream()
        .filter(az -> az.name.equals("PlacementAZ 1"))
        .forEach(az -> az.masterStsIndex = 1);

    List<TaskInfo> plan = planEdit(newIntent, pi);
    String newMasters = expectedMasterAddresses(pi);
    Set<String> tservers =
        ImmutableSet.of("yb-tserver-0_az-1", "yb-tserver-0_az-2", "yb-tserver-0_az-3");

    // The az-4 master moves in first, then az-1's masters are replaced.
    List<TaskInfo> adds = changeMasterConfigs(plan, "AddMaster");
    List<TaskInfo> removes = changeMasterConfigs(plan, "RemoveMaster");
    assertEquals(
        ImmutableList.of("yb-master-0_az-4", "yb-master-1-0_az-1"),
        adds.stream()
            .map(t -> t.getTaskParams().path("nodeName").asText())
            .collect(Collectors.toList()));
    assertEquals(
        ImmutableList.of("yb-master-0_az-3", "yb-master-0_az-1"),
        removes.stream()
            .map(t -> t.getTaskParams().path("nodeName").asText())
            .collect(Collectors.toList()));
    List<TaskInfo> pushes = masterAddrPushes(plan);

    // Around the az-3 remove, the live Raft config goes to the masters that run then. The old az-1
    // master is still in it, and the new one is not yet.
    List<TaskInfo> firstPreRemove =
        between(pushes, adds.get(0).getPosition(), removes.get(0).getPosition());
    List<TaskInfo> firstPostRemove =
        between(pushes, removes.get(0).getPosition(), adds.get(1).getPosition());
    for (List<TaskInfo> push : ImmutableList.of(firstPreRemove, firstPostRemove)) {
      assertEquals(
          ImmutableSet.of("yb-master-0_az-2", "yb-master-0_az-4"),
          nodeNames(push, ServerType.MASTER));
      assertEquals(tservers, nodeNames(push, ServerType.TSERVER));
    }
    List<TaskInfo> postRemove =
        between(pushes, removes.get(1).getPosition(), Integer.MAX_VALUE).stream()
            .filter(t -> t.getPosition() <= lastPosition(removes) + 4)
            .collect(Collectors.toList());
    Set<String> masters =
        ImmutableSet.of("yb-master-1-0_az-1", "yb-master-0_az-2", "yb-master-0_az-4");
    List<TaskInfo> secondPreRemove =
        between(pushes, adds.get(1).getPosition(), removes.get(1).getPosition());
    for (List<TaskInfo> push : ImmutableList.of(secondPreRemove, postRemove)) {
      assertEquals(masters, nodeNames(push, ServerType.MASTER));
      assertEquals(tservers, nodeNames(push, ServerType.TSERVER));
    }

    // One early Secret update, before the old az-1 master is removed, once every new master runs.
    // az-4 gained a master and az-1's release is rewritten by the full move.
    List<TaskInfo> earlySecretUpdate =
        earlySecretUpdate(plan, lastPosition(secondPreRemove), removes.get(1).getPosition());
    assertEquals(ImmutableSet.of("az-2", "az-3"), azCodes(earlySecretUpdate));
    for (TaskInfo t : earlySecretUpdate) {
      assertEquals(newMasters, helmMasterAddresses(t));
    }
    // None in the az-4 master step.
    assertTrue(
        earlySecretUpdate(plan, adds.get(0).getPosition(), removes.get(0).getPosition()).isEmpty());
  }

  @Test
  public void testZoneReplaceWithTserverResourceChangeKeepsPodsAtTheirRevision() {
    setupMasterMoveUniverse(FULL_MOVE_VERSION);
    UniverseDefinitionTaskParams.UserIntent newIntent = currentIntent();
    K8SNodeResourceSpec spec = new K8SNodeResourceSpec();
    spec.cpuCoreCount = newIntent.tserverK8SNodeResourceSpec.cpuCoreCount + 1;
    spec.memoryGib = newIntent.tserverK8SNodeResourceSpec.memoryGib;
    newIntent.tserverK8SNodeResourceSpec = spec;
    assertEarlySecretUpdateKeepsPodsAtTheirRevision(newIntent);
  }

  /** A master resource change also restarts every pod later in the edit. */
  @Test
  public void testZoneReplaceWithMasterResourceChangeKeepsPodsAtTheirRevision() {
    setupMasterMoveUniverse(FULL_MOVE_VERSION);
    UniverseDefinitionTaskParams.UserIntent newIntent = currentIntent();
    assertNotNull(newIntent.masterK8SNodeResourceSpec);
    K8SNodeResourceSpec spec = new K8SNodeResourceSpec();
    spec.cpuCoreCount = newIntent.masterK8SNodeResourceSpec.cpuCoreCount;
    spec.memoryGib = newIntent.masterK8SNodeResourceSpec.memoryGib + 1;
    newIntent.masterK8SNodeResourceSpec = spec;
    assertEarlySecretUpdateKeepsPodsAtTheirRevision(newIntent);
  }

  private void assertEarlySecretUpdateKeepsPodsAtTheirRevision(
      UniverseDefinitionTaskParams.UserIntent newIntent) {
    List<TaskInfo> plan = planEdit(newIntent, zoneReplacePlacement());

    assertPushesAroundMasterChange(
        plan,
        ImmutableSet.of("yb-master-0_az-1", "yb-master-0_az-2", "yb-master-0_az-4"),
        ImmutableSet.of("yb-tserver-0_az-1", "yb-tserver-0_az-2", "yb-tserver-0_az-3"));
    // The render carries the new resources into the pod templates, but the partitions stay at the
    // pod counts, so no pod moves to them before its roll.
    List<TaskInfo> earlySecretUpdate = earlySecretUpdate(plan);
    assertEquals(ImmutableSet.of("az-1", "az-2", "az-3"), azCodes(earlySecretUpdate));
    for (TaskInfo t : earlySecretUpdate) {
      assertTrue(keepsGflagsChecksum(t));
      assertEquals(1, t.getTaskParams().path("masterPartition").asInt());
      assertEquals(1, t.getTaskParams().path("tserverPartition").asInt());
    }
  }

  @Test
  public void testMasterOnlyFullMoveMasterAddressPropagation() {
    setupMasterMoveUniverse(FULL_MOVE_VERSION);
    UniverseDefinitionTaskParams.UserIntent newIntent = currentIntent();
    TestUtils.updateDeviceInfo(newIntent, ServerType.MASTER, d -> d.storageClass = "fast");
    PlacementInfo pi = currentPlacement();
    pi.azStream().forEach(az -> az.masterStsIndex = 1);

    List<TaskInfo> plan = planEdit(newIntent, pi);
    String newMasters = expectedMasterAddresses(pi);
    assertEquals(ImmutableSet.of("yb-master-1-0"), masterHosts(newMasters));

    // The tservers stay where they are and get both pushes.
    List<TaskInfo> postRemove =
        assertPushesAroundMasterChange(
            plan,
            ImmutableSet.of("yb-master-1-0_az-1", "yb-master-1-0_az-2", "yb-master-1-0_az-3"),
            ImmutableSet.of("yb-tserver-0_az-1", "yb-tserver-0_az-2", "yb-tserver-0_az-3"));
    List<TaskInfo> afterMastersJoined =
        between(helmUpgrades(plan), lastPosition(postRemove), Integer.MAX_VALUE);
    assertFalse(afterMastersJoined.isEmpty());
    for (TaskInfo t : afterMastersJoined) {
      assertEquals(newMasters, helmMasterAddresses(t));
    }
    // The pod delete and final render of each AZ keep the checksum, so no tserver pod rolls.
    List<TaskInfo> masterRenders =
        afterMastersJoined.stream()
            .filter(t -> "MASTER".equals(t.getTaskParams().path("serverType").asText()))
            .collect(Collectors.toList());
    assertEquals(6, masterRenders.size());
    for (TaskInfo t : masterRenders) {
      assertTrue(keepsGflagsChecksum(t));
      assertEquals(1, t.getTaskParams().path("tserverPartition").asInt());
      assertEquals(0, helmAZ(t).path("tsStsIndex").asInt());
    }
  }

  @Test
  public void testTserverOnlyFullMoveDoesNotTouchMasterAddresses() {
    setupMasterMoveUniverse(FULL_MOVE_VERSION);
    UniverseDefinitionTaskParams.UserIntent newIntent = currentIntent();
    TestUtils.updateDeviceInfo(newIntent, ServerType.TSERVER, d -> d.storageClass = "fast");
    PlacementInfo pi = currentPlacement();
    pi.azStream().forEach(az -> az.tsStsIndex = 1);

    List<TaskInfo> plan = planEdit(newIntent, pi);

    assertTrue(changeMasterConfigs(plan, "AddMaster").isEmpty());
    assertTrue(changeMasterConfigs(plan, "RemoveMaster").isEmpty());
    assertTrue(masterAddrPushes(plan).isEmpty());
    String masters = expectedMasterAddresses(pi);
    assertEquals(ImmutableSet.of("yb-master-0"), masterHosts(masters));
    List<TaskInfo> helm = helmUpgrades(plan);
    assertFalse(helm.isEmpty());
    for (TaskInfo t : helm) {
      assertEquals(masters, helmMasterAddresses(t));
      assertEquals("TSERVER", t.getTaskParams().path("serverType").asText());
      assertFalse(keepsGflagsChecksum(t));
    }
  }

  /** Adds a read replica with two tservers in az-1 and returns their node names. */
  private Set<String> addReadReplica() {
    UniverseDefinitionTaskParams.UserIntent rrIntent = currentIntent();
    rrIntent.numNodes = 2;
    rrIntent.replicationFactor = 1;
    PlacementInfo rrPlacement = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(
        AvailabilityZone.getByCode(kubernetesProvider, "az-1").getUuid(), rrPlacement, 1, 2, true);
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            ApiUtils.mockUniverseUpdaterWithReadReplica(rrIntent, rrPlacement));
    Cluster readReplica = defaultUniverse.getUniverseDetails().getReadOnlyClusters().get(0);
    Set<String> rrTservers =
        defaultUniverse.getUniverseDetails().getNodesInCluster(readReplica.uuid).stream()
            .map(n -> n.nodeName)
            .collect(Collectors.toSet());
    assertEquals(2, rrTservers.size());
    return rrTservers;
  }

  /** An edit of only the primary still edits the read replica when the masters move. */
  @Test
  public void testZoneReplaceWithoutReadReplicaInRequest() {
    setupMasterMoveUniverse(FULL_MOVE_VERSION);
    Set<String> rrTservers = addReadReplica();
    Set<String> masters =
        ImmutableSet.of("yb-master-0_az-1", "yb-master-0_az-2", "yb-master-0_az-4");
    Set<String> tservers =
        Sets.union(
            ImmutableSet.of("yb-tserver-0_az-1", "yb-tserver-0_az-2", "yb-tserver-0_az-3"),
            rrTservers);

    TaskInfo firstAttempt = submitEdit(currentIntent(), zoneReplacePlacement(), null);
    assertTrue(describeFailure(firstAttempt), describeFailure(firstAttempt).contains("stop after"));
    assertPushesAroundMasterChange(plan(firstAttempt), masters, tservers);
    TaskInfo retry = retryEdit(firstAttempt);
    assertTrue(describeFailure(retry), describeFailure(retry).contains("stop after"));
    assertPushesAroundMasterChange(plan(retry), masters, tservers);
  }

  @Test
  public void testZoneReplacePushesToReadReplicaTservers() {
    setupMasterMoveUniverse(FULL_MOVE_VERSION);
    Set<String> rrTservers = addReadReplica();
    Cluster readReplica = defaultUniverse.getUniverseDetails().getReadOnlyClusters().get(0);

    PlacementInfo pi = zoneReplacePlacement();
    java.util.function.Function<UUID, TaskInfo> submit =
        previousTaskUUID -> {
          UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
          taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
          taskParams.expectedUniverseVersion = -1;
          taskParams.nodeDetailsSet = defaultUniverse.getUniverseDetails().nodeDetailsSet;
          taskParams.setPreviousTaskUUID(previousTaskUUID);
          // Like the UI, the edit carries the read replica too.
          taskParams.clusters = new ArrayList<>(Collections.singletonList(readReplica));
          TaskInfo taskInfo = submitTask(taskParams, currentIntent(), pi);
          assertEquals(Failure, taskInfo.getTaskState());
          return taskInfo;
        };
    Set<String> masters =
        ImmutableSet.of("yb-master-0_az-1", "yb-master-0_az-2", "yb-master-0_az-4");

    // Read replica tservers get both pushes along with the primary ones.
    TaskInfo firstAttempt = submit.apply(null);
    assertPushesAroundMasterChange(
        plan(firstAttempt),
        masters,
        Sets.union(
            ImmutableSet.of("yb-tserver-0_az-1", "yb-tserver-0_az-2", "yb-tserver-0_az-3"),
            rrTservers));
    // A retry that gets here has not touched any tserver yet, the read replica's included.
    assertPushesAroundMasterChange(
        plan(retryEdit(firstAttempt)),
        masters,
        Sets.union(
            ImmutableSet.of("yb-tserver-0_az-1", "yb-tserver-0_az-2", "yb-tserver-0_az-3"),
            rrTservers));
  }

  @Test
  public void testZoneReplaceSkipsServersNotYetCreated() {
    setupMasterMoveUniverse(FULL_MOVE_VERSION);
    // The universe already lists a pod this edit has not created: it has its DNS address but no
    // pod behind it yet.
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            u -> {
              NodeDetails planned = u.getNode("yb-tserver-0_az-1").clone();
              planned.nodeName = "yb-tserver-1_az-1";
              planned.nodeUuid = UUID.randomUUID();
              planned.cloudInfo.private_ip = "yb-tserver-1_az-1.svc.cluster.local";
              planned.state = NodeDetails.NodeState.ToBeAdded;
              u.getUniverseDetails().nodeDetailsSet.add(planned);
            });
    assertNotNull(defaultUniverse.getNode("yb-tserver-1_az-1"));

    List<TaskInfo> plan = planEdit(currentIntent(), zoneReplacePlacement());

    assertPushesAroundMasterChange(
        plan,
        ImmutableSet.of("yb-master-0_az-1", "yb-master-0_az-2", "yb-master-0_az-4"),
        ImmutableSet.of("yb-tserver-0_az-1", "yb-tserver-0_az-2", "yb-tserver-0_az-3"));
  }

  private String podJson(String hostname, String ip, String namespace) {
    return "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", \"podIP\": \""
        + ip
        + "\"}, \"spec\": {\"hostname\": \""
        + hostname
        + "\"}, \"metadata\": {\"namespace\": \""
        + namespace
        + "\"}}";
  }

  /**
   * Runs a zone replace of az-3 by az-4 against a fake master quorum, through the master move, up
   * to the helm upgrade of the new az-4 tserver. Records the config changes, the in-memory pushes
   * and the helm upgrades in events. A push to one of downHosts fails.
   */
  private TaskInfo runZoneReplaceThroughMasterMove(
      PlacementInfo pi, List<String> events, Set<String> downHosts) throws Exception {
    factory.globalRuntimeConf().setValue("yb.checks.change_master_config.enabled", "false");
    factory.globalRuntimeConf().setValue("yb.kubernetes.wait_for_gflag_sync_sec", "0");

    // az-1..az-3 run a master and a tserver; az-4 gets only its new master at this stage.
    when(mockKubernetesManager.getPodInfos(any(), any(), any()))
        .thenAnswer(
            inv -> {
              String ns = inv.getArgument(2);
              String pods =
                  ns.endsWith("az-4")
                      ? podJson("yb-master-0", "192.0.2.41", ns)
                      : podJson("yb-master-0", "192.0.2.11", ns)
                          + ","
                          + podJson("yb-tserver-0", "192.0.2.12", ns);
              return TestUtils.deserialize("{\"items\": [" + pods + "]}", PodList.class).getItems();
            });

    // Fake quorum that follows the config changes. It is seeded on the first change with the
    // masters the universe knows then, which by that point carry their POD_INFO addresses.
    Map<String, Integer> voters = Collections.synchronizedMap(new java.util.LinkedHashMap<>());
    UUID universeUUID = defaultUniverse.getUniverseUUID();
    when(mockClient.changeMasterConfig(any(), anyInt(), anyBoolean(), anyBoolean(), any()))
        .thenAnswer(
            inv -> {
              String host = inv.getArgument(0);
              int port = inv.getArgument(1);
              boolean isAdd = inv.getArgument(2);
              if (voters.isEmpty()) {
                Universe.getOrBadRequest(universeUUID).getMasters().stream()
                    .filter(n -> !n.cloudInfo.private_ip.equals(host))
                    .forEach(n -> voters.put(n.cloudInfo.private_ip, n.masterRpcPort));
              }
              if (isAdd) {
                voters.put(host, port);
              } else {
                voters.remove(host);
              }
              events.add((isAdd ? "ADD " : "REMOVE ") + host);
              return null;
            });
    when(mockClient.listMasterRaftPeers())
        .thenAnswer(
            inv -> {
              List<PeerInfo> peers = new ArrayList<>();
              synchronized (voters) {
                voters.forEach(
                    (host, port) -> {
                      PeerInfo peer = new PeerInfo();
                      peer.setLastKnownPrivateIps(
                          ImmutableList.of(HostAndPort.fromParts(host, port)));
                      peers.add(peer);
                    });
              }
              ListMasterRaftPeersResponse response = mock(ListMasterRaftPeersResponse.class);
              when(response.getPeersList()).thenReturn(peers);
              return response;
            });
    when(mockClient.waitForMaster(any(), anyLong())).thenReturn(true);
    when(mockYBClient.getClientWithConfig(any())).thenReturn(mockClient);
    when(mockClient.setFlag(any(), any(), any(), anyBoolean()))
        .thenAnswer(
            inv -> {
              HostAndPort hp = inv.getArgument(0);
              if (downHosts.contains(hp.getHost())) {
                return false;
              }
              events.add(
                  "SET "
                      + hp.getHost()
                      + " "
                      + inv.getArgument(1)
                      + " "
                      + sortedCsv(inv.getArgument(2)));
              return true;
            });
    // Record the helm upgrades from the master add on, and stop at the first one after the
    // remove: the new az-4 tserver.
    doAnswer(
            inv -> {
              String ns = inv.getArgument(4);
              if (events.stream().anyMatch(e -> e.startsWith("REMOVE")) && ns.endsWith("az-4")) {
                throw new RuntimeException("stop after the master move");
              }
              if (events.stream().anyMatch(e -> e.startsWith("ADD"))) {
                events.add("HELM " + ns.substring(ns.length() - 4));
              }
              return null;
            })
        .when(mockKubernetesManager)
        .helmUpgrade(any(), any(), any(), any(), any(), any(), any());
    doNothing()
        .when(mockKubernetesManager)
        .helmInstall(any(), any(), any(), any(), any(), any(), any());

    return submitEdit(currentIntent(), pi, null);
  }

  /** The address the run-time tests reach pod 0 of the server in the AZ at. */
  private static String podHost(String server, String az) {
    return String.format("%s-0.%ss.%s-%s.svc.cluster.local", server, server, NODE_PREFIX, az);
  }

  /** A leaving tserver that is down does not fail the master move, which used to not touch it. */
  @Test
  public void testZoneReplaceWithLeavingTserverDownMovesMasters() throws Exception {
    setupMasterMoveUniverse(FULL_MOVE_VERSION);
    String leavingTserver = podHost("yb-tserver", "az-3");
    String masterC = podHost("yb-master", "az-3");
    List<String> events = Collections.synchronizedList(new ArrayList<>());

    TaskInfo taskInfo =
        runZoneReplaceThroughMasterMove(
            zoneReplacePlacement(), events, ImmutableSet.of(leavingTserver));

    assertTrue(describeFailure(taskInfo), describeFailure(taskInfo).contains("stop after"));
    assertTrue(events.toString(), events.contains("REMOVE " + masterC));
    assertEquals(
        Boolean.TRUE,
        Json.fromJson(
                TaskInfo.getOrBadRequest(taskInfo.getUuid()).getTaskParams(),
                UniverseDefinitionTaskParams.class)
            .getPrimaryCluster()
            .getMasterAddrsPropagated());
  }

  /** A tserver that stays must get the new masters, so the edit stops before any remove. */
  @Test
  public void testZoneReplaceWithStayingTserverDownFailsBeforeRemove() throws Exception {
    setupMasterMoveUniverse(FULL_MOVE_VERSION);
    String stayingTserver = podHost("yb-tserver", "az-1");
    List<String> events = Collections.synchronizedList(new ArrayList<>());

    TaskInfo taskInfo =
        runZoneReplaceThroughMasterMove(
            zoneReplacePlacement(), events, ImmutableSet.of(stayingTserver));

    assertEquals(Failure, taskInfo.getTaskState());
    assertFalse(describeFailure(taskInfo), describeFailure(taskInfo).contains("stop after"));
    assertTrue(events.toString(), events.stream().anyMatch(e -> e.startsWith("ADD")));
    assertTrue(events.toString(), events.stream().noneMatch(e -> e.startsWith("REMOVE")));
  }

  /**
   * Runs a zone replace through the early Secret update, against a fake master quorum, and checks
   * the values each server is sent and when. A retry from there pushes and renders nothing again.
   */
  @Test
  public void testZoneReplacePushedMasterAddressesAtRunTime() throws Exception {
    setupMasterMoveUniverse(FULL_MOVE_VERSION);
    PlacementInfo pi = zoneReplacePlacement();
    String newMasters = expectedMasterAddresses(pi);
    UUID universeUUID = defaultUniverse.getUniverseUUID();
    List<String> events = Collections.synchronizedList(new ArrayList<>());
    TaskInfo taskInfo = runZoneReplaceThroughMasterMove(pi, events, Collections.emptySet());
    assertTrue(describeFailure(taskInfo), describeFailure(taskInfo).contains("stop after"));

    Universe universe = Universe.getOrBadRequest(universeUUID);
    java.util.function.Function<String, String> ip = n -> universe.getNode(n).cloudInfo.private_ip;
    String masterA = ip.apply("yb-master-0_az-1");
    String masterB = ip.apply("yb-master-0_az-2");
    String masterC = ip.apply("yb-master-0_az-3");
    String masterD = ip.apply("yb-master-0_az-4");
    Set<String> pushedMasters = ImmutableSet.of(masterA, masterB, masterD);
    Set<String> pushedTservers =
        ImmutableSet.of(
            ip.apply("yb-tserver-0_az-1"),
            ip.apply("yb-tserver-0_az-2"),
            ip.apply("yb-tserver-0_az-3"));

    List<String> log = new ArrayList<>(events);
    int add = log.indexOf("ADD " + masterD);
    int remove = log.indexOf("REMOVE " + masterC);
    int earlySecretUpdate = add + 1;
    while (log.get(earlySecretUpdate).startsWith("SET")) {
      earlySecretUpdate++;
    }
    assertEquals(
        ImmutableList.of("ADD " + masterD, "REMOVE " + masterC),
        log.stream()
            .filter(e -> e.startsWith("ADD") || e.startsWith("REMOVE"))
            .collect(Collectors.toList()));
    // Every AZ but az-4 renders the new list between the pre-remove push and the remove.
    assertEquals(
        ImmutableSet.of("HELM az-1", "HELM az-2", "HELM az-3"),
        new java.util.HashSet<>(log.subList(earlySecretUpdate, remove)));
    List<String> preRemove = log.subList(add + 1, earlySecretUpdate);
    List<String> postRemove = log.subList(remove + 1, log.size());

    // Before the remove: the live Raft config, old masters plus the new one.
    String raftConfig =
        ImmutableList.of(masterA, masterB, masterC, masterD).stream()
            .map(h -> h + ":" + universe.getNode("yb-master-0_az-1").masterRpcPort)
            .collect(Collectors.joining(","));
    assertPushedTo(preRemove, pushedMasters, pushedTservers, raftConfig);
    // After the remove: the Raft config again, now the new list, before anything else touches the
    // pods.
    assertPushedTo(postRemove, pushedMasters, pushedTservers, newMasters);

    // The attempt saved that it propagated the new masters, so a retry, which may follow the
    // later tserver changes, neither pushes nor renders the early Secret update again.
    UniverseDefinitionTaskParams retryParams =
        Json.fromJson(
            TaskInfo.getOrBadRequest(taskInfo.getUuid()).getTaskParams(),
            UniverseDefinitionTaskParams.class);
    assertEquals(Boolean.TRUE, retryParams.getPrimaryCluster().getMasterAddrsPropagated());
    retryParams.setPreviousTaskUUID(taskInfo.getUuid());
    doThrow(new RuntimeException("stop after planning"))
        .when(mockKubernetesManager)
        .helmUpgrade(any(), any(), any(), any(), any(), any(), any());
    TaskInfo retryInfo =
        waitForTask(commissioner.submit(TaskType.EditKubernetesUniverse, retryParams));
    assertEquals(Failure, retryInfo.getTaskState());
    List<TaskInfo> retry = plan(retryInfo);
    int dataMove = firstPosition(subtasks(retry, TaskType.WaitForDataMove, p -> true));
    assertTrue(between(masterAddrPushes(retry), 0, dataMove).isEmpty());
    assertTrue(subtasks(retry, TaskType.UpdateParentTaskParams, p -> true).isEmpty());
    assertTrue(
        helmUpgradesInAZs(
                between(helmUpgrades(retry), 0, dataMove), ImmutableSet.of("az-1", "az-2", "az-3"))
            .isEmpty());
  }

  private static String sortedCsv(String csv) {
    return Arrays.stream(csv.split(",")).sorted().collect(Collectors.joining(","));
  }

  private static void assertPushedTo(
      List<String> events, Set<String> masters, Set<String> tservers, String masterAddresses) {
    Set<String> expected = new java.util.HashSet<>();
    String value = sortedCsv(masterAddresses);
    masters.forEach(h -> expected.add("SET " + h + " master_addresses " + value));
    tservers.forEach(h -> expected.add("SET " + h + " tserver_master_addrs " + value));
    assertEquals(expected.size(), events.size());
    assertEquals(expected, new java.util.HashSet<>(events));
  }
}
