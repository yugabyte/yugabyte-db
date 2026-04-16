// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesWaitForPod;
import com.yugabyte.yw.common.RegexMatcher;
import com.yugabyte.yw.forms.KubernetesToggleImmutableYbcParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class KubernetesToggleImmutableYbcTest extends KubernetesUpgradeTaskTest {

  private KubernetesToggleImmutableYbc kubernetesToggleImmutableYbc;

  // Expected subtasks for rolling upgrade (only tservers are upgraded, not masters)
  private static final List<TaskType> ENABLE_ROLLING_UPGRADE_TASK_SEQUENCE =
      ImmutableList.of(
          TaskType.CheckNodesAreSafeToTakeDown,
          TaskType.UpdateConsistencyCheck,
          TaskType.FreezeUniverse,
          TaskType.KubernetesCommandExecutor, // POD_INFO
          TaskType.ModifyBlackList,
          // tserver-0 start
          TaskType.CheckUnderReplicatedTablets,
          TaskType.CheckNodesAreSafeToTakeDown,
          TaskType.ModifyBlackList,
          TaskType.WaitForLeaderBlacklistCompletion,
          TaskType.KubernetesCommandExecutor, // HELM_UPGRADE for tserver-0
          TaskType.KubernetesWaitForPod,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.WaitStartingFromTime,
          TaskType.ModifyBlackList,
          TaskType.CheckFollowerLag,
          // tserver-1 start
          TaskType.CheckUnderReplicatedTablets,
          TaskType.CheckNodesAreSafeToTakeDown,
          TaskType.ModifyBlackList,
          TaskType.WaitForLeaderBlacklistCompletion,
          TaskType.KubernetesCommandExecutor, // HELM_UPGRADE for tserver-1
          TaskType.KubernetesWaitForPod,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.WaitStartingFromTime,
          TaskType.ModifyBlackList,
          TaskType.CheckFollowerLag,
          // tserver-2 start
          TaskType.CheckUnderReplicatedTablets,
          TaskType.CheckNodesAreSafeToTakeDown,
          TaskType.ModifyBlackList,
          TaskType.WaitForLeaderBlacklistCompletion,
          TaskType.KubernetesCommandExecutor, // HELM_UPGRADE for tserver-2
          TaskType.KubernetesWaitForPod,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.WaitStartingFromTime,
          TaskType.ModifyBlackList,
          TaskType.CheckFollowerLag,
          // rolling updates end
          TaskType.WaitForYbcServer,
          TaskType.LoadBalancerStateChange,
          TaskType.UpdateAndPersistKubernetesImmutableYbc, // persist immutable ybc
          TaskType.UniverseUpdateSucceeded,
          TaskType.ModifyBlackList);

  @Before
  public void setUp() {
    super.setUp();
    setUnderReplicatedTabletsMock();
    setFollowerLagMock();
    this.kubernetesToggleImmutableYbc =
        new KubernetesToggleImmutableYbc(
            mockBaseTaskDependencies, mockOperatorStatusUpdaterFactory);
  }

  private TaskInfo submitTask(KubernetesToggleImmutableYbcParams taskParams) {
    return submitTask(taskParams, TaskType.KubernetesToggleImmutableYbc, commissioner);
  }

  private static List<JsonNode> createRollingUpgradeResult(boolean isSingleAZ) {
    String namespace = isSingleAZ ? "demo-universe" : "demo-universe-az-2";
    return ImmutableList.of(
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(
            ImmutableMap.of("commandType", KubernetesCommandExecutor.CommandType.POD_INFO.name())),
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
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()));
  }

  @Test
  public void testToggleImmutableYbcRollingUpgradeSingleAZ() {
    setupUniverseSingleAZ(false, false);
    UniverseUpdater updater =
        u -> {
          UniverseDefinitionTaskParams params = u.getUniverseDetails();
          params.setEnableYbc(true);
          params.setYbcInstalled(true);
          params.setYbcSoftwareVersion("2.0.0.0-b0");
        };
    defaultUniverse = Universe.saveDetails(defaultUniverse.getUniverseUUID(), updater, false);
    kubernetesToggleImmutableYbc.setUserTaskUUID(UUID.randomUUID());

    ArgumentCaptor<String> expectedYbSoftwareVersion = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedPodName = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map<String, String>> expectedConfig = ArgumentCaptor.forClass(Map.class);
    ArgumentCaptor<UUID> expectedUniverseUUID = ArgumentCaptor.forClass(UUID.class);

    String overrideFileRegex = "(.*)" + defaultUniverse.getUniverseUUID() + "(.*).yml";

    KubernetesToggleImmutableYbcParams taskParams = new KubernetesToggleImmutableYbcParams();
    taskParams.setUseYbdbInbuiltYbc(true);
    TaskInfo taskInfo = submitTask(taskParams);

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

    assertEquals(YB_SOFTWARE_VERSION_OLD, expectedYbSoftwareVersion.getValue());
    assertEquals(config, expectedConfig.getValue());
    assertEquals(NODE_PREFIX, expectedNodePrefix.getValue());
    assertEquals(NODE_PREFIX, expectedNamespace.getValue());
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertTaskSequence(
        subTasksByPosition, ENABLE_ROLLING_UPGRADE_TASK_SEQUENCE, createRollingUpgradeResult(true));
    assertEquals(Success, taskInfo.getTaskState());
  }

  @Test
  public void testToggleImmutableYbcRollingUpgradeMultiAZ() {
    setupUniverseMultiAZ(false, false);
    UniverseUpdater updater =
        u -> {
          UniverseDefinitionTaskParams params = u.getUniverseDetails();
          params.setEnableYbc(true);
          params.setYbcInstalled(true);
          params.setYbcSoftwareVersion("2.0.0.0-b0");
        };
    defaultUniverse = Universe.saveDetails(defaultUniverse.getUniverseUUID(), updater, false);
    kubernetesToggleImmutableYbc.setUserTaskUUID(UUID.randomUUID());

    ArgumentCaptor<String> expectedYbSoftwareVersion = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedPodName = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map<String, String>> expectedConfig = ArgumentCaptor.forClass(Map.class);
    ArgumentCaptor<UUID> expectedUniverseUUID = ArgumentCaptor.forClass(UUID.class);

    String overrideFileRegex = "(.*)" + defaultUniverse.getUniverseUUID() + "(.*).yml";

    KubernetesToggleImmutableYbcParams taskParams = new KubernetesToggleImmutableYbcParams();
    taskParams.setUseYbdbInbuiltYbc(true);
    TaskInfo taskInfo = submitTask(taskParams);

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
    assertTaskSequence(
        subTasksByPosition,
        ENABLE_ROLLING_UPGRADE_TASK_SEQUENCE,
        createRollingUpgradeResult(false));
    assertEquals(Success, taskInfo.getTaskState());
  }

  @Test
  public void testToggleImmutableYbcRetries() {
    setupUniverseMultiAZ(false, false);
    kubernetesToggleImmutableYbc.setUserTaskUUID(UUID.randomUUID());
    KubernetesToggleImmutableYbcParams taskParams = new KubernetesToggleImmutableYbcParams();
    taskParams.setUseYbdbInbuiltYbc(true);
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = 2;
    taskParams.sleepAfterMasterRestartMillis = 0;
    taskParams.sleepAfterTServerRestartMillis = 0;
    super.verifyTaskRetries(
        defaultCustomer,
        CustomerTask.TaskType.KubernetesToggleImmutableYbc,
        CustomerTask.TargetType.Universe,
        defaultUniverse.getUniverseUUID(),
        TaskType.KubernetesToggleImmutableYbc,
        taskParams);
    checkUniverseNodesStates(taskParams.getUniverseUUID());
  }
}
