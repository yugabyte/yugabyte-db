// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCheckNumPod.CommandType.WAIT_FOR_PODS;
import static com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor.CommandType.APPLY_SECRET;
import static com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor.CommandType.CREATE_NAMESPACE;
import static com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor.CommandType.HELM_INSTALL;
import static com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor.CommandType.POD_INFO;
import static com.yugabyte.yw.common.ApiUtils.getTestUserIntent;
import static com.yugabyte.yw.common.AssertHelper.assertJsonEqual;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThat;
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
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.RegexMatcher;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import io.fabric8.kubernetes.api.model.Pod;
import io.fabric8.kubernetes.api.model.PodList;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.CommonTypes.TableType;
import org.yb.client.ChangeMasterClusterConfigResponse;
import org.yb.client.ListTabletServersResponse;
import org.yb.client.YBClient;
import org.yb.client.YBTable;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class CreateKubernetesUniverseTest extends CommissionerBaseTest {

  private Universe defaultUniverse;
  private Integer universeVersion = 2;

  private static final String NODE_PREFIX = "demo-universe";
  private static final String YB_SOFTWARE_VERSION = "1.0.0";
  private String nodePrefix1, nodePrefix2, nodePrefix3;
  private String ns, ns1, ns2, ns3;
  private Map<String, String> expectedNodeNameToIP;

  private AvailabilityZone az1, az2, az3;
  private Map<String, String> config = new HashMap<>();
  private Map<String, String> config1 = new HashMap<>();
  private Map<String, String> config2 = new HashMap<>();
  private Map<String, String> config3 = new HashMap<>();

  private String universeName = "TestUniverse";

  private YBClient mockClient;

  @Before
  public void setUp() {
    super.setUp();
    when(mockOperatorStatusUpdaterFactory.create()).thenReturn(mockOperatorStatusUpdater);
  }

  private void setupUniverseMultiAZ(
      boolean setMasters, boolean enabledYEDIS, boolean setNamespace, boolean newNamingStyle) {
    Region r = Region.create(kubernetesProvider, "region-1", "PlacementRegion-1", "default-image");
    az1 = AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ-1", "subnet-1");
    az2 = AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ-2", "subnet-2");
    az3 = AvailabilityZone.createOrThrow(r, "az-3", "PlacementAZ-3", "subnet-3");
    InstanceType i =
        InstanceType.upsert(
            kubernetesProvider.getUuid(),
            "c3.xlarge",
            10,
            5.5,
            new InstanceType.InstanceTypeDetails());
    UniverseDefinitionTaskParams.UserIntent userIntent =
        getTestUserIntent(r, kubernetesProvider, i, 3);
    userIntent.replicationFactor = 3;
    userIntent.masterGFlags = new HashMap<>();
    userIntent.tserverGFlags = new HashMap<>();
    userIntent.universeName = "demo-universe";
    userIntent.ybSoftwareVersion = YB_SOFTWARE_VERSION;
    userIntent.enableYEDIS = enabledYEDIS;
    defaultUniverse = createUniverse(defaultCustomer.getId());
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            ApiUtils.mockUniverseUpdater(userIntent, NODE_PREFIX, setMasters /* setMasters */));
    defaultUniverse.updateConfig(
        ImmutableMap.of(Universe.HELM2_LEGACY, Universe.HelmLegacy.V3.toString()));
    defaultUniverse.save();

    nodePrefix1 = String.format("%s-%s", NODE_PREFIX, az1.getCode());
    nodePrefix2 = String.format("%s-%s", NODE_PREFIX, az2.getCode());
    nodePrefix3 = String.format("%s-%s", NODE_PREFIX, az3.getCode());
    ns1 = nodePrefix1;
    ns2 = nodePrefix2;
    ns3 = nodePrefix3;

    if (setNamespace) {
      ns1 = "demo-ns-1";
      ns2 = "demons2";

      config1.put("KUBECONFIG", "test-kc-" + 1);
      config2.put("KUBECONFIG", "test-kc-" + 2);
      config3.put("KUBECONFIG", "test-kc-" + 3);

      config1.put("KUBENAMESPACE", ns1);
      config2.put("KUBENAMESPACE", ns2);

      az1.updateConfig(config1);
      az1.save();
      az2.updateConfig(config2);
      az2.save();
      az3.updateConfig(config3);
      az3.save();

    } else {
      config.put("KUBECONFIG", "test");
      kubernetesProvider.setConfigMap(config);
      kubernetesProvider.save();

      // Copying provider config
      config1.putAll(config);
      config2.putAll(config);
      config3.putAll(config);
    }

    String helmNameSuffix1 = "";
    String helmNameSuffix2 = "";
    String helmNameSuffix3 = "";
    if (newNamingStyle) {
      ns1 = NODE_PREFIX;
      ns2 = NODE_PREFIX;
      ns3 = NODE_PREFIX;

      helmNameSuffix1 = nodePrefix1 + "-yugabyte-";
      helmNameSuffix2 = nodePrefix2 + "-yugabyte-";
      helmNameSuffix3 = nodePrefix3 + "-yugabyte-";
    }

    // <helm suffix>yb-<server>-<id>_<az>
    String nodeNameFormat = "yb-%s-%d_%s";
    // <helm suffix>yb-<server>-<id>.<helm suffix>yb-<server>s.<namespace>.svc.cluster.local
    String privateIPFormat = "%syb-%s-%d.%1$syb-%2$ss.%s.svc.cluster.local";
    expectedNodeNameToIP =
        ImmutableMap.<String, String>builder()
            .put(
                String.format(nodeNameFormat, "master", 0, az1.getCode()),
                String.format(privateIPFormat, helmNameSuffix1, "master", 0, ns1))
            .put(
                String.format(nodeNameFormat, "tserver", 0, az1.getCode()),
                String.format(privateIPFormat, helmNameSuffix1, "tserver", 0, ns1))
            .put(
                String.format(nodeNameFormat, "master", 0, az2.getCode()),
                String.format(privateIPFormat, helmNameSuffix2, "master", 0, ns2))
            .put(
                String.format(nodeNameFormat, "tserver", 0, az2.getCode()),
                String.format(privateIPFormat, helmNameSuffix2, "tserver", 0, ns2))
            .put(
                String.format(nodeNameFormat, "master", 0, az3.getCode()),
                String.format(privateIPFormat, helmNameSuffix3, "master", 0, ns3))
            .put(
                String.format(nodeNameFormat, "tserver", 0, az3.getCode()),
                String.format(privateIPFormat, helmNameSuffix3, "tserver", 0, ns3))
            .build();

    String podInfosMessage =
        "{\"items\": [{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\","
            + " \"podIP\": \"123.456.78.90\"}, \"spec\": {\"hostname\": \"%1$syb-master-0\"},"
            + " \"metadata\": {\"namespace\": \"%2$s\"}},"
            + "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", "
            + "\"podIP\": \"123.456.78.91\"}, \"spec\": {\"hostname\": \"%1$syb-tserver-0\"},"
            + " \"metadata\": {\"namespace\": \"%2$s\"}}]}";

    List<Pod> pods1 =
        TestUtils.deserialize(String.format(podInfosMessage, helmNameSuffix1, ns1), PodList.class)
            .getItems();
    when(mockKubernetesManager.getPodInfos(
            any(),
            eq(
                (newNamingStyle
                    ? "yb"
                        + universeName.toLowerCase().substring(0, 11)
                        + "-"
                        + az1.getCode()
                        + "-"
                        + Util.base36hash(nodePrefix1)
                    : nodePrefix1)),
            eq(ns1)))
        .thenReturn(pods1);
    List<Pod> pods2 =
        TestUtils.deserialize(String.format(podInfosMessage, helmNameSuffix2, ns2), PodList.class)
            .getItems();
    when(mockKubernetesManager.getPodInfos(
            any(),
            eq(
                (newNamingStyle
                    ? "yb"
                        + universeName.toLowerCase().substring(0, 11)
                        + "-"
                        + az2.getCode()
                        + "-"
                        + Util.base36hash(nodePrefix2)
                    : nodePrefix2)),
            eq(ns2)))
        .thenReturn(pods2);
    List<Pod> pods3 =
        TestUtils.deserialize(String.format(podInfosMessage, helmNameSuffix3, ns3), PodList.class)
            .getItems();
    when(mockKubernetesManager.getPodInfos(
            any(),
            eq(
                (newNamingStyle
                    ? "yb"
                        + universeName.toLowerCase().substring(0, 11)
                        + "-"
                        + az3.getCode()
                        + "-"
                        + Util.base36hash(nodePrefix3)
                    : nodePrefix3)),
            eq(ns3)))
        .thenReturn(pods3);
  }

  private void setupUniverse(boolean setMasters, boolean enabledYEDIS, boolean setNamespace) {
    Region r = Region.create(kubernetesProvider, "region-1", "PlacementRegion-1", "default-image");
    AvailabilityZone az = AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ-1", "subnet-1");
    InstanceType i =
        InstanceType.upsert(
            kubernetesProvider.getUuid(),
            "c3.xlarge",
            10,
            5.5,
            new InstanceType.InstanceTypeDetails());
    UniverseDefinitionTaskParams.UserIntent userIntent =
        getTestUserIntent(r, kubernetesProvider, i, 3);
    userIntent.replicationFactor = 3;
    userIntent.masterGFlags = new HashMap<>();
    userIntent.tserverGFlags = new HashMap<>();
    userIntent.universeName = "demo-universe";
    userIntent.ybSoftwareVersion = YB_SOFTWARE_VERSION;
    userIntent.enableYEDIS = enabledYEDIS;
    defaultUniverse = createUniverse(defaultCustomer.getId());
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        ApiUtils.mockUniverseUpdater(userIntent, NODE_PREFIX, setMasters /* setMasters */));
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    defaultUniverse.updateConfig(
        ImmutableMap.of(Universe.HELM2_LEGACY, Universe.HelmLegacy.V3.toString()));
    defaultUniverse.save();

    ns = NODE_PREFIX;
    if (setNamespace) {
      ns = "demo-ns";
      config.put("KUBECONFIG", "test-kc");
      config.put("KUBENAMESPACE", ns);
      az.updateConfig(config);
      az.save();
    } else {
      config.put("KUBECONFIG", "test");
      kubernetesProvider.setConfigMap(config);
      kubernetesProvider.save();
    }

    String podsString =
        "{\"items\": [{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", "
            + "\"podIP\": \"1.2.3.1\"}, \"spec\": {\"hostname\": \"yb-master-0\"},"
            + " \"metadata\": {\"namespace\": \""
            + ns
            + "\"}},"
            + "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", "
            + "\"podIP\": \"1.2.3.2\"}, \"spec\": {\"hostname\": \"yb-tserver-0\"},"
            + " \"metadata\": {\"namespace\": \""
            + ns
            + "\"}},"
            + "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", "
            + "\"podIP\": \"1.2.3.3\"}, \"spec\": {\"hostname\": \"yb-master-1\"},"
            + " \"metadata\": {\"namespace\": \""
            + ns
            + "\"}},"
            + "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", "
            + "\"podIP\": \"1.2.3.4\"}, \"spec\": {\"hostname\": \"yb-tserver-1\"},"
            + " \"metadata\": {\"namespace\": \""
            + ns
            + "\"}},"
            + "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", "
            + "\"podIP\": \"1.2.3.5\"}, \"spec\": {\"hostname\": \"yb-master-2\"},"
            + " \"metadata\": {\"namespace\": \""
            + ns
            + "\"}},"
            + "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", "
            + "\"podIP\": \"1.2.3.6\"}, \"spec\": {\"hostname\": \"yb-tserver-2\"},"
            + " \"metadata\": {\"namespace\": \""
            + ns
            + "\"}}]}";
    List<Pod> pods = TestUtils.deserialize(podsString, PodList.class).getItems();
    when(mockKubernetesManager.getPodInfos(any(), any(), any())).thenReturn(pods);
  }

  private void setupCommon() {
    // Table RPCs.
    mockClient = mock(YBClient.class);
    // WaitForTServerHeartBeats mock.
    ListTabletServersResponse mockResponse = mock(ListTabletServersResponse.class);
    when(mockResponse.getTabletServersCount()).thenReturn(3);
    when(mockClient.waitForServer(any(), anyLong())).thenReturn(true);
    when(mockYBClient.getClient(any(), any())).thenReturn(mockClient);
    YBTable mockTable = mock(YBTable.class);
    when(mockTable.getName()).thenReturn("redis");
    when(mockTable.getTableType()).thenReturn(TableType.REDIS_TABLE_TYPE);
    ChangeMasterClusterConfigResponse ccr = new ChangeMasterClusterConfigResponse(1111, "", null);
    try {
      when(mockClient.changeMasterClusterConfig(any())).thenReturn(ccr);
      when(mockClient.listTabletServers()).thenReturn(mockResponse);
      when(mockClient.createRedisTable(any(), anyBoolean())).thenReturn(mockTable);
    } catch (Exception e) {
    }
    when(mockNodeUniverseManager.runYsqlCommand(any(), any(), any(), any()))
        .thenReturn(
            ShellResponse.create(ShellResponse.ERROR_CODE_SUCCESS, "Command output: CREATE TABLE"));
    // WaitForServer mock.
    mockWaits(mockClient);
  }

  private static final List<TaskType> KUBERNETES_CREATE_UNIVERSE_TASKS =
      ImmutableList.of(
          TaskType.KubernetesCommandExecutor,
          TaskType.KubernetesCommandExecutor,
          TaskType.KubernetesCommandExecutor,
          TaskType.KubernetesCheckNumPod,
          TaskType.KubernetesCommandExecutor,
          TaskType.InstallingThirdPartySoftware,
          TaskType.WaitForServer,
          TaskType.WaitForMasterLeader,
          TaskType.UpdatePlacementInfo,
          TaskType.WaitForTServerHeartBeats,
          TaskType.SwamperTargetsFileUpdate,
          TaskType.CreateAlertDefinitions,
          TaskType.CreateTable,
          TaskType.CreateTable,
          TaskType.UniverseUpdateSucceeded);

  private static final ImmutableMap<String, String> EXPECTED_RESULT_FOR_CREATE_TABLE_TASK =
      ImmutableMap.of("tableType", "REDIS_TABLE_TYPE", "tableName", "redis");

  // Cannot use defaultUniverse.universeUUID in a class field.
  private List<JsonNode> getExpectedCreateUniverseTaskResults() {
    return ImmutableList.of(
        Json.toJson(ImmutableMap.of("commandType", CREATE_NAMESPACE.name())),
        Json.toJson(ImmutableMap.of("commandType", APPLY_SECRET.name())),
        Json.toJson(ImmutableMap.of("commandType", HELM_INSTALL.name())),
        Json.toJson(ImmutableMap.of("commandType", WAIT_FOR_PODS.name())),
        Json.toJson(ImmutableMap.of("commandType", POD_INFO.name())),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of("removeFile", false)),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(EXPECTED_RESULT_FOR_CREATE_TABLE_TASK),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()));
  }

  private List<Integer> getTaskPositionsToSkip(boolean skipNamespace) {
    // 3 is WAIT_FOR_PODS of type KubernetesCheckNumPod task.
    // 0 is CREATE_NAMESPACE of type KubernetesCommandExecutor
    return skipNamespace ? ImmutableList.of(0, 3) : ImmutableList.of(3);
  }

  private List<Integer> getTaskCountPerPosition(int namespaceTasks, int parallelTasks) {
    return ImmutableList.of(
        namespaceTasks, parallelTasks, parallelTasks, 0, 1, 1, 3, 1, 1, 1, 1, 1, 1, 1, 1);
  }

  private void assertTaskSequence(
      Map<Integer, List<TaskInfo>> subTasksByPosition,
      List<TaskType> expectedTasks,
      List<JsonNode> expectedTasksResult,
      List<Integer> taskPositionsToSkip,
      List<Integer> taskCountPerPosition) {
    int position = 0;
    for (TaskType taskType : expectedTasks) {
      if (taskPositionsToSkip.contains(position)) {
        position++;
        continue;
      }

      List<TaskInfo> tasks = subTasksByPosition.get(position);
      JsonNode expectedResults = expectedTasksResult.get(position);
      List<JsonNode> taskDetails =
          tasks.stream().map(TaskInfo::getDetails).collect(Collectors.toList());
      int expectedSize = taskCountPerPosition.get(position);
      assertEquals(expectedSize, tasks.size());
      assertEquals(taskType, tasks.get(0).getTaskType());
      assertJsonEqual(expectedResults, taskDetails.get(0));
      position++;
    }
  }

  private TaskInfo submitTask(UniverseDefinitionTaskParams taskParams) {
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.nodePrefix = "demo-universe";
    taskParams.expectedUniverseVersion = universeVersion;
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.nodeDetailsSet = defaultUniverse.getUniverseDetails().nodeDetailsSet;
    try {
      UUID taskUUID = commissioner.submit(TaskType.CreateKubernetesUniverse, taskParams);
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    return null;
  }

  @Test
  public void testCreateKubernetesUniverseSuccessMultiAZ() {
    testCreateKubernetesUniverseSuccessMultiAZBase(false, false);
  }

  @Test
  public void testCreateKubernetesUniverseSuccessMultiAZWithNamespace() {
    testCreateKubernetesUniverseSuccessMultiAZBase(true, false);
  }

  @Test
  public void testCreateKubernetesUniverseSuccessMultiAZNewNaming() {
    testCreateKubernetesUniverseSuccessMultiAZBase(false, true);
  }

  private void testCreateKubernetesUniverseSuccessMultiAZBase(
      boolean setNamespace, boolean newNamingStyle) {
    setupUniverseMultiAZ(
        /* Create Masters */ false, /* YEDIS/REDIS enabled */ true, setNamespace, newNamingStyle);
    setupCommon();

    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    if (newNamingStyle) {
      taskParams.useNewHelmNamingStyle = true;
    }
    TaskInfo taskInfo = submitTask(taskParams);
    if (newNamingStyle) {
      verify(mockKubernetesManager, times(3)).createNamespace(config1, ns1);
    } else if (setNamespace) {
      verify(mockKubernetesManager, times(0)).createNamespace(config1, ns1);
      verify(mockKubernetesManager, times(0)).createNamespace(config2, ns2);
      verify(mockKubernetesManager, times(1)).createNamespace(config3, ns3);
    } else {
      verify(mockKubernetesManager, times(1)).createNamespace(config1, ns1);
      verify(mockKubernetesManager, times(1)).createNamespace(config2, ns2);
      verify(mockKubernetesManager, times(1)).createNamespace(config3, ns3);
    }

    verify(mockKubernetesManager, times(1))
        .helmInstall(
            eq(defaultUniverse.getUniverseUUID()),
            eq(YB_SOFTWARE_VERSION),
            eq(config1),
            eq(kubernetesProvider.getUuid()),
            eq(
                (newNamingStyle
                    ? "yb"
                        + universeName.toLowerCase().substring(0, 11)
                        + "-"
                        + az1.getCode()
                        + "-"
                        + Util.base36hash(nodePrefix1)
                    : nodePrefix1)),
            eq(newNamingStyle ? NODE_PREFIX : ns1),
            expectedOverrideFile.capture());
    verify(mockKubernetesManager, times(1))
        .helmInstall(
            eq(defaultUniverse.getUniverseUUID()),
            eq(YB_SOFTWARE_VERSION),
            eq(config2),
            eq(kubernetesProvider.getUuid()),
            eq(
                (newNamingStyle
                    ? "yb"
                        + universeName.toLowerCase().substring(0, 11)
                        + "-"
                        + az2.getCode()
                        + "-"
                        + Util.base36hash(nodePrefix2)
                    : nodePrefix2)),
            eq(newNamingStyle ? NODE_PREFIX : ns2),
            expectedOverrideFile.capture());
    verify(mockKubernetesManager, times(1))
        .helmInstall(
            eq(defaultUniverse.getUniverseUUID()),
            eq(YB_SOFTWARE_VERSION),
            eq(config3),
            eq(kubernetesProvider.getUuid()),
            eq(
                (newNamingStyle
                    ? "yb"
                        + universeName.toLowerCase().substring(0, 11)
                        + "-"
                        + az3.getCode()
                        + "-"
                        + Util.base36hash(nodePrefix3)
                    : nodePrefix3)),
            eq(newNamingStyle ? NODE_PREFIX : ns3),
            expectedOverrideFile.capture());

    String overrideFileRegex = "(.*)" + defaultUniverse.getUniverseUUID() + "(.*).yml";
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));

    verify(mockKubernetesManager, times(1))
        .getPodInfos(
            config1,
            newNamingStyle
                ? "yb"
                    + universeName.toLowerCase().substring(0, 11)
                    + "-"
                    + az1.getCode()
                    + "-"
                    + Util.base36hash(nodePrefix1)
                : nodePrefix1,
            newNamingStyle ? NODE_PREFIX : ns1);
    verify(mockKubernetesManager, times(1))
        .getPodInfos(
            config2,
            newNamingStyle
                ? "yb"
                    + universeName.toLowerCase().substring(0, 11)
                    + "-"
                    + az2.getCode()
                    + "-"
                    + Util.base36hash(nodePrefix2)
                : nodePrefix2,
            newNamingStyle ? NODE_PREFIX : ns2);
    verify(mockKubernetesManager, times(1))
        .getPodInfos(
            config3,
            newNamingStyle
                ? "yb"
                    + universeName.toLowerCase().substring(0, 11)
                    + "-"
                    + az3.getCode()
                    + "-"
                    + Util.base36hash(nodePrefix3)
                : nodePrefix3,
            newNamingStyle ? NODE_PREFIX : ns3);
    verify(mockSwamperHelper, times(1)).writeUniverseTargetJson(defaultUniverse.getUniverseUUID());

    Universe u = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    Map<String, String> nodeNameToIP =
        u.getUniverseDetails().nodeDetailsSet.stream()
            .collect(Collectors.toMap(n -> n.nodeName, n -> n.cloudInfo.private_ip));
    assertEquals(expectedNodeNameToIP, nodeNameToIP);
    assertEquals(newNamingStyle, u.getUniverseDetails().useNewHelmNamingStyle);

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    int numNamespaces = setNamespace ? 1 : 3;
    assertTaskSequence(
        subTasksByPosition,
        KUBERNETES_CREATE_UNIVERSE_TASKS,
        getExpectedCreateUniverseTaskResults(),
        getTaskPositionsToSkip(/* skip namespace task */ false),
        getTaskCountPerPosition(numNamespaces, 3));
    assertEquals(Success, taskInfo.getTaskState());
  }

  @Test
  public void testCreateKubernetesUniverseSuccessSingleAZ() {
    testCreateKubernetesUniverseSuccessSingleAZBase(false);
  }

  @Test
  public void testCreateKubernetesUniverseSuccessSingleAZWithNamespace() {
    testCreateKubernetesUniverseSuccessSingleAZBase(true);
  }

  private void testCreateKubernetesUniverseSuccessSingleAZBase(boolean setNamespace) {
    setupUniverse(/* Create Masters */ false, /* YEDIS/REDIS enabled */ true, setNamespace);
    setupCommon();
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    TaskInfo taskInfo = submitTask(taskParams);

    if (setNamespace) {
      verify(mockKubernetesManager, times(0)).createNamespace(config, ns);
    } else {
      verify(mockKubernetesManager, times(1)).createNamespace(config, ns);
    }

    verify(mockKubernetesManager, times(1))
        .helmInstall(
            eq(defaultUniverse.getUniverseUUID()),
            eq(YB_SOFTWARE_VERSION),
            eq(config),
            eq(kubernetesProvider.getUuid()),
            eq(NODE_PREFIX),
            eq(ns),
            expectedOverrideFile.capture());

    String overrideFileRegex = "(.*)" + defaultUniverse.getUniverseUUID() + "(.*).yml";
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));

    verify(mockKubernetesManager, times(1)).getPodInfos(config, NODE_PREFIX, ns);
    verify(mockSwamperHelper, times(1)).writeUniverseTargetJson(defaultUniverse.getUniverseUUID());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    int numNamespaces = setNamespace ? 0 : 1;
    assertTaskSequence(
        subTasksByPosition,
        KUBERNETES_CREATE_UNIVERSE_TASKS,
        getExpectedCreateUniverseTaskResults(),
        getTaskPositionsToSkip(/* skip namespace task */ setNamespace),
        getTaskCountPerPosition(numNamespaces, 1));
    assertEquals(Success, taskInfo.getTaskState());
  }

  @Test
  public void testCreateKubernetesUniverseFailure() {
    setupUniverse(
        /* Create Masters */ true, /* YEDIS/REDIS enabled */ true, /* set namespace */ false);
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Failure, taskInfo.getTaskState());
  }

  @Test
  public void testCreateKubernetesUniverseMultiAZWithoutYedis() {
    setupUniverseMultiAZ(
        /* Create Masters */ false, /* YEDIS/REDIS disabled */
        false, /* set namespace */
        false, /* new naming */
        false);
    testCreateKubernetesUniverseSubtasksWithoutYedis(3);
  }

  @Test
  public void testCreateKubernetesUniverseSingleAZWithoutYedis() {
    setupUniverse(
        /* Create Masters */ false, /* YEDIS/REDIS disabled */ false, /* set namespace */ false);
    testCreateKubernetesUniverseSubtasksWithoutYedis(1);
  }

  private void testCreateKubernetesUniverseSubtasksWithoutYedis(int tasksNum) {
    setupCommon();
    TaskInfo taskInfo = submitTask(new UniverseDefinitionTaskParams());

    List<TaskType> createUniverseTasks = new ArrayList<>(KUBERNETES_CREATE_UNIVERSE_TASKS);
    int createTableTaskIndex = createUniverseTasks.indexOf(TaskType.CreateTable);
    createUniverseTasks.remove(TaskType.CreateTable);

    List<JsonNode> expectedResults = new ArrayList<>(getExpectedCreateUniverseTaskResults());
    expectedResults.remove(Json.toJson(EXPECTED_RESULT_FOR_CREATE_TABLE_TASK));

    List<Integer> taskCountPerPosition =
        new ArrayList<>(getTaskCountPerPosition(tasksNum, tasksNum));
    taskCountPerPosition.remove(createTableTaskIndex);

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertTaskSequence(
        subTasksByPosition,
        createUniverseTasks,
        expectedResults,
        getTaskPositionsToSkip(/* skip namespace task */ false),
        taskCountPerPosition);
    assertEquals(Success, taskInfo.getTaskState());
  }

  @Test
  public void testCreateKubernetesUniverseWithPodAddrTemplate() {
    setupUniverseMultiAZ(
        false /* Create Masters */,
        true /* YEDIS/REDIS enabled */,
        false /* set namespace */,
        false /* new naming */);
    Map<String, String> azConfig =
        ImmutableMap.of("KUBE_POD_ADDRESS_TEMPLATE", "{pod_name}.{namespace}.svc.{cluster_domain}");
    az1.updateConfig(azConfig);
    az1.save();
    az2.updateConfig(azConfig);
    az2.save();
    az3.updateConfig(azConfig);
    az3.save();

    setupCommon();

    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    TaskInfo taskInfo = submitTask(taskParams);

    String masters =
        String.format(
            "yb-master-0.%s.svc.cluster.local:7100,"
                + "yb-master-0.%s.svc.cluster.local:7100,"
                + "yb-master-0.%s.svc.cluster.local:7100",
            ns1, ns2, ns3);
    verify(mockYBClient, times(7)).getClient(masters, null);

    long timeout = 300000;
    verify(mockClient, times(1))
        .waitForServer(
            HostAndPort.fromParts("yb-tserver-0." + ns1 + ".svc.cluster.local", 9100), timeout);
    verify(mockClient, times(1))
        .waitForServer(
            HostAndPort.fromParts("yb-tserver-0." + ns2 + ".svc.cluster.local", 9100), timeout);
    verify(mockClient, times(1))
        .waitForServer(
            HostAndPort.fromParts("yb-tserver-0." + ns3 + ".svc.cluster.local", 9100), timeout);

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertTaskSequence(
        subTasksByPosition,
        KUBERNETES_CREATE_UNIVERSE_TASKS,
        getExpectedCreateUniverseTaskResults(),
        getTaskPositionsToSkip(/* skip namespace task */ false),
        getTaskCountPerPosition(3, 3));
    assertEquals(Success, taskInfo.getTaskState());
  }
}
