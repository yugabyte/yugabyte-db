// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.common.ApiUtils.getTestUserIntent;
import static com.yugabyte.yw.common.AssertHelper.assertJsonEqual;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesWaitForPod;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.RegexMatcher;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskType;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import io.fabric8.kubernetes.api.model.Pod;
import io.fabric8.kubernetes.api.model.PodList;
import io.fabric8.kubernetes.client.utils.Serialization;
import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.client.IsInitDbDoneResponse;
import org.yb.client.IsServerReadyResponse;
import org.yb.client.UpgradeYsqlResponse;
import org.yb.client.YBClient;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
@Slf4j
public class UpgradeKubernetesUniverseTest extends CommissionerBaseTest {

  private UpgradeKubernetesUniverse upgradeUniverse;

  private YBClient mockClient;

  private Universe defaultUniverse;

  private static final String NODE_PREFIX = "demo-universe";
  private static final String YB_SOFTWARE_VERSION_OLD = "old-version";
  private static final String YB_SOFTWARE_VERSION_NEW = "new-version";

  private Map<String, String> config = new HashMap<>();

  @Before
  public void setUp() {
    super.setUp();
    setFollowerLagMock();
    setUnderReplicatedTabletsMock();
    when(mockOperatorStatusUpdaterFactory.create()).thenReturn(mockOperatorStatusUpdater);
    this.upgradeUniverse =
        new UpgradeKubernetesUniverse(mockBaseTaskDependencies, mockOperatorStatusUpdaterFactory);
  }

  private void setupUniverse(
      boolean setMasters, UserIntent userIntent, PlacementInfo placementInfo) {
    upgradeUniverse.setUserTaskUUID(UUID.randomUUID());
    userIntent.replicationFactor = 3;
    userIntent.masterGFlags = new HashMap<>();
    userIntent.tserverGFlags = new HashMap<>();
    userIntent.universeName = "demo-universe";
    userIntent.ybSoftwareVersion = YB_SOFTWARE_VERSION_OLD;
    defaultUniverse = createUniverse(defaultCustomer.getId());
    config.put("KUBECONFIG", "test");
    kubernetesProvider.setConfigMap(config);
    kubernetesProvider.save();
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        ApiUtils.mockUniverseUpdater(
            userIntent, NODE_PREFIX, setMasters /* setMasters */, false, placementInfo));
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    defaultUniverse.updateConfig(
        ImmutableMap.of(Universe.HELM2_LEGACY, Universe.HelmLegacy.V3.toString()));
    defaultUniverse.save();

    try {
      File jsonFile = new File("src/test/resources/testPod.json");
      InputStream jsonStream = new FileInputStream(jsonFile);

      Pod testPod = Serialization.unmarshal(jsonStream, Pod.class);
      when(mockKubernetesManager.getPodObject(any(), any(), any())).thenReturn(testPod);
    } catch (Exception e) {
    }

    YBClient mockClient = mock(YBClient.class);
    when(mockYBClient.getClientWithConfig(any())).thenReturn(mockClient);
    when(mockClient.waitForServer(any(), anyLong())).thenReturn(true);
    String masterLeaderName = "yb-master-0.yb-masters.demo-universe.svc.cluster.local";
    if (placementInfo.cloudList.get(0).regionList.get(0).azList.size() > 1) {
      masterLeaderName = "yb-master-0.yb-masters.demo-universe-az-2.svc.cluster.local";
    }
    IsServerReadyResponse okReadyResp = new IsServerReadyResponse(0, "", null, 0, 0);
    UpgradeYsqlResponse mockUpgradeYsqlResponse = new UpgradeYsqlResponse(1000, "", null);
    IsInitDbDoneResponse mockIsInitDbDoneResponse =
        new IsInitDbDoneResponse(1000, "", true, true, null, null);
    try {
      when(mockClient.waitForMaster(any(), anyLong())).thenReturn(true);
      when(mockClient.isServerReady(any(), anyBoolean())).thenReturn(okReadyResp);
      when(mockClient.upgradeYsql(any(HostAndPort.class), anyBoolean()))
          .thenReturn(mockUpgradeYsqlResponse);
      when(mockClient.getIsInitDbDone()).thenReturn(mockIsInitDbDoneResponse);
      when(mockClient.getLeaderMasterHostAndPort())
          .thenReturn(HostAndPort.fromString(masterLeaderName));
    } catch (Exception ex) {
      fail();
    }
    when(mockYBClient.getClient(any(), any())).thenReturn(mockClient);
  }

  private void setupUniverseSingleAZ(boolean setMasters) {
    Region r = Region.create(kubernetesProvider, "region-1", "PlacementRegion-1", "default-image");
    AvailabilityZone az1 = AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ-1", "subnet-1");
    InstanceType i =
        InstanceType.upsert(
            kubernetesProvider.getUuid(),
            "c3.xlarge",
            10,
            5.5,
            new InstanceType.InstanceTypeDetails());
    UserIntent userIntent = getTestUserIntent(r, kubernetesProvider, i, 3);
    PlacementInfo pi = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az1.getUuid(), pi, 3, 3, true);
    setupUniverse(setMasters, userIntent, pi);
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
            + "\"podIP\": \"1.2.3.3\"}, \"spec\": {\"hostname\": \"yb-master-1\"},"
            + " \"metadata\": {\"namespace\": \""
            + NODE_PREFIX
            + "\"}},"
            + "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", "
            + "\"podIP\": \"1.2.3.4\"}, \"spec\": {\"hostname\": \"yb-tserver-1\"},"
            + " \"metadata\": {\"namespace\": \""
            + NODE_PREFIX
            + "\"}},"
            + "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", "
            + "\"podIP\": \"1.2.3.5\"}, \"spec\": {\"hostname\": \"yb-master-2\"},"
            + " \"metadata\": {\"namespace\": \""
            + NODE_PREFIX
            + "\"}},"
            + "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", "
            + "\"podIP\": \"1.2.3.6\"}, \"spec\": {\"hostname\": \"yb-tserver-2\"},"
            + " \"metadata\": {\"namespace\": \""
            + NODE_PREFIX
            + "\"}}]}";
    List<Pod> pods = TestUtils.deserialize(podsString, PodList.class).getItems();
    when(mockKubernetesManager.getPodInfos(any(), any(), any())).thenReturn(pods);
  }

  private void setupUniverseMultiAZ(boolean setMasters) {
    Region r = Region.create(kubernetesProvider, "region-1", "PlacementRegion-1", "default-image");
    AvailabilityZone az1 = AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ-1", "subnet-1");
    AvailabilityZone az2 = AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ-2", "subnet-2");
    AvailabilityZone az3 = AvailabilityZone.createOrThrow(r, "az-3", "PlacementAZ-3", "subnet-3");
    InstanceType i =
        InstanceType.upsert(
            kubernetesProvider.getUuid(),
            "c3.xlarge",
            10,
            5.5,
            new InstanceType.InstanceTypeDetails());
    UserIntent userIntent = getTestUserIntent(r, kubernetesProvider, i, 3);
    PlacementInfo pi = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az1.getUuid(), pi, 1, 1, false);
    PlacementInfoUtil.addPlacementZone(az2.getUuid(), pi, 1, 1, true);
    PlacementInfoUtil.addPlacementZone(az3.getUuid(), pi, 1, 1, false);
    setupUniverse(setMasters, userIntent, pi);

    String nodePrefix1 = String.format("%s-%s", NODE_PREFIX, az1.getCode());
    String nodePrefix2 = String.format("%s-%s", NODE_PREFIX, az2.getCode());
    String nodePrefix3 = String.format("%s-%s", NODE_PREFIX, az3.getCode());

    String podInfosMessage =
        "{\"items\": [{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", "
            + "\"podIP\": \"1.2.3.1\"}, \"spec\": {\"hostname\": \"yb-master-0\"},"
            + " \"metadata\": {\"namespace\": \"%1$s\"}},"
            + "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", "
            + "\"podIP\": \"1.2.3.2\"}, \"spec\": {\"hostname\": \"yb-tserver-0\"},"
            + " \"metadata\": {\"namespace\": \"%1$s\"}}]}";
    List<Pod> pods1 =
        TestUtils.deserialize(String.format(podInfosMessage, nodePrefix1), PodList.class)
            .getItems();
    when(mockKubernetesManager.getPodInfos(any(), eq(nodePrefix1), eq(nodePrefix1)))
        .thenReturn(pods1);
    List<Pod> pods2 =
        TestUtils.deserialize(String.format(podInfosMessage, nodePrefix2), PodList.class)
            .getItems();
    when(mockKubernetesManager.getPodInfos(any(), eq(nodePrefix2), eq(nodePrefix2)))
        .thenReturn(pods2);
    List<Pod> pods3 =
        TestUtils.deserialize(String.format(podInfosMessage, nodePrefix3), PodList.class)
            .getItems();
    when(mockKubernetesManager.getPodInfos(any(), eq(nodePrefix3), eq(nodePrefix3)))
        .thenReturn(pods3);
  }

  private static final List<TaskType> KUBERNETES_UPGRADE_SOFTWARE_TASKS =
      ImmutableList.of(
          TaskType.FreezeUniverse,
          TaskType.KubernetesCommandExecutor,
          TaskType.KubernetesCommandExecutor,
          TaskType.KubernetesWaitForPod,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.WaitStartingFromTime,
          TaskType.CheckFollowerLag,
          TaskType.KubernetesCommandExecutor,
          TaskType.KubernetesWaitForPod,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.WaitStartingFromTime,
          TaskType.CheckFollowerLag,
          TaskType.KubernetesCommandExecutor,
          TaskType.KubernetesWaitForPod,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.WaitStartingFromTime,
          TaskType.CheckFollowerLag,
          TaskType.LoadBalancerStateChange,
          TaskType.CheckUnderReplicatedTablets,
          TaskType.KubernetesCommandExecutor,
          TaskType.KubernetesWaitForPod,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.WaitStartingFromTime,
          TaskType.CheckFollowerLag,
          TaskType.CheckUnderReplicatedTablets,
          TaskType.KubernetesCommandExecutor,
          TaskType.KubernetesWaitForPod,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.WaitStartingFromTime,
          TaskType.CheckFollowerLag,
          TaskType.CheckUnderReplicatedTablets,
          TaskType.KubernetesCommandExecutor,
          TaskType.KubernetesWaitForPod,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.WaitStartingFromTime,
          TaskType.CheckFollowerLag,
          TaskType.LoadBalancerStateChange,
          TaskType.RunYsqlUpgrade,
          TaskType.UpdateSoftwareVersion,
          TaskType.UniverseUpdateSucceeded);

  private static final List<TaskType> KUBERNETES_UPGRADE_GFLAG_TASKS =
      ImmutableList.of(
          TaskType.FreezeUniverse,
          TaskType.UpdateAndPersistGFlags,
          TaskType.KubernetesCommandExecutor,
          TaskType.KubernetesCommandExecutor,
          TaskType.KubernetesWaitForPod,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.WaitStartingFromTime,
          TaskType.CheckFollowerLag,
          TaskType.KubernetesCommandExecutor,
          TaskType.KubernetesWaitForPod,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.WaitStartingFromTime,
          TaskType.CheckFollowerLag,
          TaskType.KubernetesCommandExecutor,
          TaskType.KubernetesWaitForPod,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.WaitStartingFromTime,
          TaskType.CheckFollowerLag,
          TaskType.LoadBalancerStateChange,
          TaskType.CheckUnderReplicatedTablets,
          TaskType.KubernetesCommandExecutor,
          TaskType.KubernetesWaitForPod,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.WaitStartingFromTime,
          TaskType.CheckFollowerLag,
          TaskType.CheckUnderReplicatedTablets,
          TaskType.KubernetesCommandExecutor,
          TaskType.KubernetesWaitForPod,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.WaitStartingFromTime,
          TaskType.CheckFollowerLag,
          TaskType.CheckUnderReplicatedTablets,
          TaskType.KubernetesCommandExecutor,
          TaskType.KubernetesWaitForPod,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.WaitStartingFromTime,
          TaskType.CheckFollowerLag,
          TaskType.LoadBalancerStateChange,
          TaskType.UniverseUpdateSucceeded);

  private static List<JsonNode> createUpgradeSoftwareResult(boolean isSingleAZ) {
    String namespace = isSingleAZ ? "demo-universe" : "demo-universe-az-2";
    return ImmutableList.of(
        Json.toJson(ImmutableMap.of()),
        Json.toJson(
            ImmutableMap.of("commandType", KubernetesCommandExecutor.CommandType.POD_INFO.name())),
        Json.toJson(
            ImmutableMap.of(
                "commandType",
                KubernetesCommandExecutor.CommandType.HELM_UPGRADE.name(),
                "ybSoftwareVersion",
                "new-version")),
        Json.toJson(
            ImmutableMap.of("commandType", KubernetesWaitForPod.CommandType.WAIT_FOR_POD.name())),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(
            ImmutableMap.of(
                "commandType",
                KubernetesCommandExecutor.CommandType.HELM_UPGRADE.name(),
                "ybSoftwareVersion",
                "new-version")),
        Json.toJson(
            ImmutableMap.of("commandType", KubernetesWaitForPod.CommandType.WAIT_FOR_POD.name())),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(
            ImmutableMap.of(
                "commandType",
                KubernetesCommandExecutor.CommandType.HELM_UPGRADE.name(),
                "ybSoftwareVersion",
                "new-version")),
        Json.toJson(
            ImmutableMap.of("commandType", KubernetesWaitForPod.CommandType.WAIT_FOR_POD.name())),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(
            ImmutableMap.of(
                "commandType",
                KubernetesCommandExecutor.CommandType.HELM_UPGRADE.name(),
                "ybSoftwareVersion",
                "new-version",
                "namespace",
                namespace)),
        Json.toJson(
            ImmutableMap.of("commandType", KubernetesWaitForPod.CommandType.WAIT_FOR_POD.name())),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(
            ImmutableMap.of(
                "commandType",
                KubernetesCommandExecutor.CommandType.HELM_UPGRADE.name(),
                "ybSoftwareVersion",
                "new-version")),
        Json.toJson(
            ImmutableMap.of("commandType", KubernetesWaitForPod.CommandType.WAIT_FOR_POD.name())),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(
            ImmutableMap.of(
                "commandType",
                KubernetesCommandExecutor.CommandType.HELM_UPGRADE.name(),
                "ybSoftwareVersion",
                "new-version")),
        Json.toJson(
            ImmutableMap.of("commandType", KubernetesWaitForPod.CommandType.WAIT_FOR_POD.name())),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()));
  }

  private static List<JsonNode> createUpdateGflagsResult(boolean isSingleAZ) {
    String namespace = isSingleAZ ? "demo-universe" : "demo-universe-az-2";
    return ImmutableList.of(
        Json.toJson(ImmutableMap.of()),
        Json.toJson(
            ImmutableMap.of(
                "masterGFlags", Json.parse("{\"master-flag\":\"m1\"}"),
                "tserverGFlags", Json.parse("{\"tserver-flag\":\"t1\"}"))),
        Json.toJson(
            ImmutableMap.of("commandType", KubernetesCommandExecutor.CommandType.POD_INFO.name())),
        Json.toJson(
            ImmutableMap.of(
                "commandType", KubernetesCommandExecutor.CommandType.HELM_UPGRADE.name())),
        Json.toJson(
            ImmutableMap.of("commandType", KubernetesWaitForPod.CommandType.WAIT_FOR_POD.name())),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(
            ImmutableMap.of(
                "commandType", KubernetesCommandExecutor.CommandType.HELM_UPGRADE.name())),
        Json.toJson(
            ImmutableMap.of("commandType", KubernetesWaitForPod.CommandType.WAIT_FOR_POD.name())),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(
            ImmutableMap.of(
                "commandType", KubernetesCommandExecutor.CommandType.HELM_UPGRADE.name())),
        Json.toJson(
            ImmutableMap.of("commandType", KubernetesWaitForPod.CommandType.WAIT_FOR_POD.name())),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(
            ImmutableMap.of(
                "commandType",
                KubernetesCommandExecutor.CommandType.HELM_UPGRADE.name(),
                "namespace",
                namespace)),
        Json.toJson(
            ImmutableMap.of("commandType", KubernetesWaitForPod.CommandType.WAIT_FOR_POD.name())),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(
            ImmutableMap.of(
                "commandType", KubernetesCommandExecutor.CommandType.HELM_UPGRADE.name())),
        Json.toJson(
            ImmutableMap.of("commandType", KubernetesWaitForPod.CommandType.WAIT_FOR_POD.name())),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(
            ImmutableMap.of(
                "commandType", KubernetesCommandExecutor.CommandType.HELM_UPGRADE.name())),
        Json.toJson(
            ImmutableMap.of("commandType", KubernetesWaitForPod.CommandType.WAIT_FOR_POD.name())),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()));
  }

  private void assertTaskSequence(
      Map<Integer, List<TaskInfo>> subTasksByPosition,
      UpgradeTaskType taskType,
      boolean isSingleAZ) {
    int position = 0;
    List<TaskType> taskList =
        taskType == UpgradeTaskParams.UpgradeTaskType.Software
            ? KUBERNETES_UPGRADE_SOFTWARE_TASKS
            : KUBERNETES_UPGRADE_GFLAG_TASKS;
    for (TaskType task : taskList) {
      List<TaskInfo> tasks = subTasksByPosition.get(position);
      assertEquals(1, tasks.size());
      assertEquals("At position " + position, task, tasks.get(0).getTaskType());
      List<JsonNode> expectedResultsList =
          taskType == UpgradeTaskParams.UpgradeTaskType.Software
              ? createUpgradeSoftwareResult(isSingleAZ)
              : createUpdateGflagsResult(isSingleAZ);
      JsonNode expectedResults = expectedResultsList.get(position);
      List<JsonNode> taskDetails =
          tasks.stream().map(TaskInfo::getDetails).collect(Collectors.toList());
      assertJsonEqual(expectedResults, taskDetails.get(0));
      position++;
    }
  }

  private TaskInfo submitTask(
      UpgradeKubernetesUniverse.Params taskParams, UpgradeTaskType taskType) {
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.taskType = taskType;
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.nodePrefix = NODE_PREFIX;
    taskParams.expectedUniverseVersion = 2;
    // Need not sleep for default 3min in tests.
    taskParams.sleepAfterMasterRestartMillis = 5;
    taskParams.sleepAfterTServerRestartMillis = 5;

    try {
      UUID taskUUID = commissioner.submit(TaskType.UpgradeKubernetesUniverse, taskParams);
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    return null;
  }

  @Test
  public void testSoftwareUpgradeSingleAZ() {
    setupUniverseSingleAZ(/* Create Masters */ false);

    ArgumentCaptor<String> expectedYbSoftwareVersion = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedPodName = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map<String, String>> expectedConfig = ArgumentCaptor.forClass(Map.class);
    ArgumentCaptor<UUID> expectedUniverseUUID = ArgumentCaptor.forClass(UUID.class);

    String overrideFileRegex = "(.*)" + defaultUniverse.getUniverseUUID() + "(.*).yml";

    UpgradeKubernetesUniverse.Params taskParams = new UpgradeKubernetesUniverse.Params();
    taskParams.ybSoftwareVersion = "new-version";
    TaskInfo taskInfo = submitTask(taskParams, UpgradeTaskParams.UpgradeTaskType.Software);

    verify(mockKubernetesManager, times(6))
        .helmUpgrade(
            expectedUniverseUUID.capture(),
            expectedYbSoftwareVersion.capture(),
            expectedConfig.capture(),
            expectedNodePrefix.capture(),
            expectedNamespace.capture(),
            expectedOverrideFile.capture());
    verify(mockKubernetesManager, times(6))
        .getPodObject(
            expectedConfig.capture(), expectedNodePrefix.capture(), expectedPodName.capture());
    verify(mockKubernetesManager, times(1))
        .getPodInfos(
            expectedConfig.capture(), expectedNodePrefix.capture(), expectedNamespace.capture());

    assertEquals(YB_SOFTWARE_VERSION_NEW, expectedYbSoftwareVersion.getValue());
    assertEquals(config, expectedConfig.getValue());
    assertEquals(NODE_PREFIX, expectedNodePrefix.getValue());
    assertEquals(NODE_PREFIX, expectedNamespace.getValue());
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertTaskSequence(subTasksByPosition, UpgradeTaskParams.UpgradeTaskType.Software, true);
    assertEquals(Success, taskInfo.getTaskState());
  }

  @Test
  public void testGFlagUpgradeSingleAZ() {
    setupUniverseSingleAZ(/* Create Masters */ false);

    ArgumentCaptor<String> expectedYbSoftwareVersion = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedPodName = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map<String, String>> expectedConfig = ArgumentCaptor.forClass(Map.class);
    ArgumentCaptor<UUID> expectedUniverseUUID = ArgumentCaptor.forClass(UUID.class);
    String overrideFileRegex = "(.*)" + defaultUniverse.getUniverseUUID() + "(.*).yml";

    UpgradeKubernetesUniverse.Params taskParams = new UpgradeKubernetesUniverse.Params();
    taskParams.masterGFlags = ImmutableMap.of("master-flag", "m1");
    taskParams.tserverGFlags = ImmutableMap.of("tserver-flag", "t1");
    TaskInfo taskInfo = submitTask(taskParams, UpgradeTaskParams.UpgradeTaskType.GFlags);

    verify(mockKubernetesManager, times(6))
        .helmUpgrade(
            expectedUniverseUUID.capture(),
            expectedYbSoftwareVersion.capture(),
            expectedConfig.capture(),
            expectedNodePrefix.capture(),
            expectedNamespace.capture(),
            expectedOverrideFile.capture());
    verify(mockKubernetesManager, times(6))
        .getPodObject(
            expectedConfig.capture(), expectedNodePrefix.capture(), expectedPodName.capture());
    verify(mockKubernetesManager, times(1))
        .getPodInfos(
            expectedConfig.capture(), expectedNodePrefix.capture(), expectedNamespace.capture());

    assertEquals(YB_SOFTWARE_VERSION_OLD, expectedYbSoftwareVersion.getValue());
    assertEquals(config, expectedConfig.getValue());
    assertEquals(NODE_PREFIX, expectedNodePrefix.getValue());
    assertEquals(NODE_PREFIX, expectedNamespace.getValue());
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertTaskSequence(subTasksByPosition, UpgradeTaskParams.UpgradeTaskType.GFlags, true);
    assertEquals(Success, taskInfo.getTaskState());
  }

  @Test
  public void testSoftwareUpgradeMultiAZ() {
    setupUniverseMultiAZ(/* Create Masters */ false);
    ArgumentCaptor<String> expectedYbSoftwareVersion = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedPodName = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map<String, String>> expectedConfig = ArgumentCaptor.forClass(Map.class);
    ArgumentCaptor<UUID> expectedUniverseUUID = ArgumentCaptor.forClass(UUID.class);

    String overrideFileRegex = "(.*)" + defaultUniverse.getUniverseUUID() + "(.*).yml";

    UpgradeKubernetesUniverse.Params taskParams = new UpgradeKubernetesUniverse.Params();
    taskParams.ybSoftwareVersion = YB_SOFTWARE_VERSION_NEW;
    TaskInfo taskInfo = submitTask(taskParams, UpgradeTaskParams.UpgradeTaskType.Software);

    verify(mockKubernetesManager, times(6))
        .helmUpgrade(
            expectedUniverseUUID.capture(),
            expectedYbSoftwareVersion.capture(),
            expectedConfig.capture(),
            expectedNodePrefix.capture(),
            expectedNamespace.capture(),
            expectedOverrideFile.capture());
    verify(mockKubernetesManager, times(6))
        .getPodObject(
            expectedConfig.capture(), expectedNodePrefix.capture(), expectedPodName.capture());
    verify(mockKubernetesManager, times(3))
        .getPodInfos(
            expectedConfig.capture(), expectedNodePrefix.capture(), expectedNamespace.capture());

    assertEquals(YB_SOFTWARE_VERSION_NEW, expectedYbSoftwareVersion.getValue());
    assertEquals(config, expectedConfig.getValue());
    assertTrue(expectedNodePrefix.getValue().contains(NODE_PREFIX));
    assertTrue(expectedNamespace.getValue().contains(NODE_PREFIX));
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertTaskSequence(subTasksByPosition, UpgradeTaskParams.UpgradeTaskType.Software, false);
    assertEquals(Success, taskInfo.getTaskState());
  }

  @Test
  public void testGFlagUpgradeMultiAZ() {
    setupUniverseMultiAZ(/* Create Masters */ false);

    ArgumentCaptor<String> expectedYbSoftwareVersion = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedPodName = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map<String, String>> expectedConfig = ArgumentCaptor.forClass(Map.class);
    ArgumentCaptor<UUID> expectedUniverseUUID = ArgumentCaptor.forClass(UUID.class);
    String overrideFileRegex = "(.*)" + defaultUniverse.getUniverseUUID() + "(.*).yml";

    UpgradeKubernetesUniverse.Params taskParams = new UpgradeKubernetesUniverse.Params();
    taskParams.masterGFlags = ImmutableMap.of("master-flag", "m1");
    taskParams.tserverGFlags = ImmutableMap.of("tserver-flag", "t1");
    TaskInfo taskInfo = submitTask(taskParams, UpgradeTaskParams.UpgradeTaskType.GFlags);

    verify(mockKubernetesManager, times(6))
        .helmUpgrade(
            expectedUniverseUUID.capture(),
            expectedYbSoftwareVersion.capture(),
            expectedConfig.capture(),
            expectedNodePrefix.capture(),
            expectedNamespace.capture(),
            expectedOverrideFile.capture());
    verify(mockKubernetesManager, times(6))
        .getPodObject(
            expectedConfig.capture(), expectedNodePrefix.capture(), expectedPodName.capture());
    verify(mockKubernetesManager, times(3))
        .getPodInfos(
            expectedConfig.capture(), expectedNodePrefix.capture(), expectedNamespace.capture());

    assertEquals(YB_SOFTWARE_VERSION_OLD, expectedYbSoftwareVersion.getValue());
    assertEquals(config, expectedConfig.getValue());
    assertTrue(expectedNodePrefix.getValue().contains(NODE_PREFIX));
    assertTrue(expectedNamespace.getValue().contains(NODE_PREFIX));
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertTaskSequence(subTasksByPosition, UpgradeTaskParams.UpgradeTaskType.GFlags, false);
    assertEquals(Success, taskInfo.getTaskState());
  }
}
