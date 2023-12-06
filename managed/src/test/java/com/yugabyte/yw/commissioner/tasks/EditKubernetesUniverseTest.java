// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCheckNumPod.CommandType.WAIT_FOR_PODS;
import static com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor.CommandType.HELM_UPGRADE;
import static com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor.CommandType.POD_INFO;
import static com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesWaitForPod.CommandType.WAIT_FOR_POD;
import static com.yugabyte.yw.common.ApiUtils.getTestUserIntent;
import static com.yugabyte.yw.common.AssertHelper.assertJsonEqual;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.RegexMatcher;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import io.fabric8.kubernetes.api.model.Pod;
import io.fabric8.kubernetes.api.model.PodList;
import io.fabric8.kubernetes.client.utils.Serialization;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.InjectMocks;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.client.ChangeMasterClusterConfigResponse;
import org.yb.client.GetLoadMovePercentResponse;
import org.yb.client.IsServerReadyResponse;
import org.yb.client.YBClient;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class EditKubernetesUniverseTest extends CommissionerBaseTest {

  @InjectMocks private EditKubernetesUniverse editUniverse;

  private Universe defaultUniverse;

  private static final String NODE_PREFIX = "demo-universe";
  private static final String YB_SOFTWARE_VERSION = "1.0.0";

  private Map<String, String> config = new HashMap<>();

  private void setup() {
    try {
      File jsonFile = new File("src/test/resources/testPod.json");
      InputStream jsonStream = new FileInputStream(jsonFile);

      Pod testPod = Serialization.unmarshal(jsonStream, Pod.class);
      when(mockKubernetesManager.getPodObject(any(), any(), any())).thenReturn(testPod);
    } catch (Exception e) {
    }
    YBClient mockClient = mock(YBClient.class);
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
    userIntent.masterGFlags = new HashMap<>();
    userIntent.tserverGFlags = new HashMap<>();
    userIntent.universeName = "demo-universe";
    userIntent.ybSoftwareVersion = YB_SOFTWARE_VERSION;
    defaultUniverse = createUniverse(defaultCustomer.getId());
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        ApiUtils.mockUniverseUpdater(userIntent, NODE_PREFIX, setMasters /* setMasters */));
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(), ApiUtils.mockUniverseUpdaterWithActivePods(1, 3));
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    defaultUniverse.updateConfig(
        ImmutableMap.of(Universe.HELM2_LEGACY, Universe.HelmLegacy.V3.toString()));
    defaultUniverse.save();
  }

  private static final List<TaskType> KUBERNETES_ADD_POD_TASKS =
      ImmutableList.of(
          TaskType.KubernetesCommandExecutor,
          TaskType.KubernetesCheckNumPod,
          TaskType.KubernetesCommandExecutor,
          TaskType.WaitForServer,
          TaskType.UpdatePlacementInfo,
          TaskType.KubernetesCommandExecutor,
          TaskType.SwamperTargetsFileUpdate,
          TaskType.UniverseUpdateSucceeded);

  private List<JsonNode> getExpectedAddPodTaskResults() {
    return ImmutableList.of(
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of("commandType", HELM_UPGRADE.name())),
        Json.toJson(ImmutableMap.of("commandType", WAIT_FOR_PODS.name())),
        Json.toJson(ImmutableMap.of("commandType", POD_INFO.name())),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of("commandType", POD_INFO.name())),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()));
  }

  private static final List<TaskType> KUBERNETES_REMOVE_POD_TASKS =
      ImmutableList.of(
          TaskType.UpdatePlacementInfo,
          TaskType.WaitForDataMove,
          TaskType.KubernetesCommandExecutor,
          TaskType.KubernetesCheckNumPod,
          TaskType.ModifyBlackList,
          TaskType.KubernetesCommandExecutor,
          TaskType.SwamperTargetsFileUpdate,
          TaskType.UniverseUpdateSucceeded);

  private List<JsonNode> getExpectedRemovePodTaskResults() {
    return ImmutableList.of(
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of("commandType", HELM_UPGRADE.name())),
        Json.toJson(ImmutableMap.of("commandType", WAIT_FOR_PODS.name())),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of("commandType", POD_INFO.name())),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()));
  }

  private static final List<TaskType> KUBERNETES_CHANGE_INSTANCE_TYPE_TASKS =
      ImmutableList.of(
          TaskType.UpdatePlacementInfo,
          TaskType.KubernetesCommandExecutor,
          TaskType.KubernetesWaitForPod,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.KubernetesCommandExecutor,
          TaskType.KubernetesWaitForPod,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.KubernetesCommandExecutor,
          TaskType.KubernetesWaitForPod,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.KubernetesCommandExecutor,
          TaskType.SwamperTargetsFileUpdate,
          TaskType.UniverseUpdateSucceeded);

  private List<JsonNode> getExpectedChangeInstaceTypeResults() {
    return ImmutableList.of(
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of("commandType", HELM_UPGRADE.name())),
        Json.toJson(ImmutableMap.of("commandType", WAIT_FOR_POD.name())),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of("commandType", HELM_UPGRADE.name())),
        Json.toJson(ImmutableMap.of("commandType", WAIT_FOR_POD.name())),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of("commandType", HELM_UPGRADE.name())),
        Json.toJson(ImmutableMap.of("commandType", WAIT_FOR_POD.name())),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of("commandType", POD_INFO.name())),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()));
  }

  private void assertTaskSequence(
      Map<Integer, List<TaskInfo>> subTasksByPosition,
      List<TaskType> taskList,
      List<JsonNode> resultList,
      String type) {
    int position = 0;
    // Since we create two empty subGroupTasks for add (namespace create and apply secret)
    // they need to skipped over.
    if (type.equals("add")) {
      position = 2;
    }
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
          tasks.stream().map(TaskInfo::getDetails).collect(Collectors.toList());
      assertJsonEqual(expectedResults, taskDetails.get(0));
      position++;

      // Similar to add, we expect two empty subGroupTasks (namespace and volume delete).
      if (position == 3 && type.equals("remove")) {
        position += 2;
      }
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
    taskParams.expectedUniverseVersion = 3;
    taskParams.nodeDetailsSet = defaultUniverse.getUniverseDetails().nodeDetailsSet;
    UniverseDefinitionTaskParams.UserIntent newUserIntent =
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.clone();
    newUserIntent.numNodes = 5;
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

    ArgumentCaptor<String> expectedYbSoftwareVersion = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map<String, String>> expectedConfig = ArgumentCaptor.forClass(Map.class);
    ArgumentCaptor<UUID> expectedUniverseUUID = ArgumentCaptor.forClass(UUID.class);

    String overrideFileRegex = "(.*)" + defaultUniverse.getUniverseUUID() + "(.*).yml";
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
            + "\"}}]}";
    List<Pod> pods = TestUtils.deserialize(podsString, PodList.class).getItems();
    when(mockKubernetesManager.getPodInfos(any(), any(), any())).thenReturn(pods);

    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = 3;
    taskParams.nodeDetailsSet = defaultUniverse.getUniverseDetails().nodeDetailsSet;
    UniverseDefinitionTaskParams.UserIntent newUserIntent =
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.clone();
    newUserIntent.numNodes = 2;
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
            + "\"}}]}";
    List<Pod> pods = TestUtils.deserialize(podsString, PodList.class).getItems();
    when(mockKubernetesManager.getPodInfos(any(), any(), any())).thenReturn(pods);

    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = 3;
    taskParams.nodeDetailsSet = defaultUniverse.getUniverseDetails().nodeDetailsSet;
    InstanceType.upsert(
        kubernetesProvider.getUuid(), "c5.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());
    UniverseDefinitionTaskParams.UserIntent newUserIntent =
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.clone();
    newUserIntent.instanceType = "c5.xlarge";
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
  public void testVolumeDecreaseIsForbidden() {
    setupUniverseSingleAZ(/* Create Masters */ true);
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = 3;
    taskParams.nodeDetailsSet = defaultUniverse.getUniverseDetails().nodeDetailsSet;
    UniverseDefinitionTaskParams.UserIntent newUserIntent =
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.clone();
    newUserIntent.instanceType = "c5.xlarge";
    newUserIntent.deviceInfo.volumeSize--;
    RuntimeConfigEntry.upsertGlobal("yb.edit.allow_volume_decrease", "true");
    PlacementInfo pi = defaultUniverse.getUniverseDetails().getPrimaryCluster().placementInfo;
    TaskInfo taskInfo = submitTask(taskParams, newUserIntent, pi);
    assertEquals(Failure, taskInfo.getTaskState());
    assertTrue(
        taskInfo.getErrorMessage().contains("Cannot decrease disk size in a Kubernetes cluster"));
  }
}
