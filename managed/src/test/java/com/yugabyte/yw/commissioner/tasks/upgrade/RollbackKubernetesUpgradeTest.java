// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesWaitForPod;
import com.yugabyte.yw.common.RegexMatcher;
import com.yugabyte.yw.common.TestHelper;
import com.yugabyte.yw.forms.RollbackUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.SoftwareUpgradeState;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.mockito.ArgumentCaptor;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import play.libs.Json;

public class RollbackKubernetesUpgradeTest extends KubernetesUpgradeTaskTest {

  @Rule public MockitoRule rule = MockitoJUnit.rule();

  private RollbackKubernetesUpgrade rollbackKubernetesUpgrade;

  private static final List<TaskType> UPGRADE_TASK_SEQUENCE =
      ImmutableList.of(
          TaskType.CheckNodesAreSafeToTakeDown,
          TaskType.FreezeUniverse,
          TaskType.UpdateConsistencyCheck,
          TaskType.UpdateUniverseState,
          TaskType.RollbackAutoFlags,
          TaskType.KubernetesCommandExecutor,
          TaskType.ModifyBlackList,
          TaskType.CheckUnderReplicatedTablets,
          TaskType.CheckNodesAreSafeToTakeDown,
          TaskType.ModifyBlackList,
          TaskType.WaitForLeaderBlacklistCompletion,
          TaskType.KubernetesCommandExecutor,
          TaskType.KubernetesWaitForPod,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.WaitStartingFromTime,
          TaskType.ModifyBlackList,
          TaskType.CheckFollowerLag,
          TaskType.CheckUnderReplicatedTablets,
          TaskType.CheckNodesAreSafeToTakeDown,
          TaskType.ModifyBlackList,
          TaskType.WaitForLeaderBlacklistCompletion,
          TaskType.KubernetesCommandExecutor,
          TaskType.KubernetesWaitForPod,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.WaitStartingFromTime,
          TaskType.ModifyBlackList,
          TaskType.CheckFollowerLag,
          TaskType.CheckUnderReplicatedTablets,
          TaskType.CheckNodesAreSafeToTakeDown,
          TaskType.ModifyBlackList,
          TaskType.WaitForLeaderBlacklistCompletion,
          TaskType.KubernetesCommandExecutor,
          TaskType.KubernetesWaitForPod,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.WaitStartingFromTime,
          TaskType.ModifyBlackList,
          TaskType.CheckFollowerLag,
          TaskType.LoadBalancerStateChange,
          TaskType.CheckNodesAreSafeToTakeDown,
          TaskType.KubernetesCommandExecutor,
          TaskType.KubernetesWaitForPod,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.WaitStartingFromTime,
          TaskType.CheckFollowerLag,
          TaskType.CheckNodesAreSafeToTakeDown,
          TaskType.KubernetesCommandExecutor,
          TaskType.KubernetesWaitForPod,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.WaitStartingFromTime,
          TaskType.CheckFollowerLag,
          TaskType.CheckNodesAreSafeToTakeDown,
          TaskType.KubernetesCommandExecutor,
          TaskType.KubernetesWaitForPod,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.WaitStartingFromTime,
          TaskType.CheckFollowerLag,
          TaskType.UpdateSoftwareVersion,
          TaskType.UpdateUniverseState,
          TaskType.UniverseUpdateSucceeded);

  private static List<JsonNode> createUpgradeResult(boolean isSingleAZ) {
    String namespace = isSingleAZ ? "demo-universe" : "demo-universe-az-2";
    return ImmutableList.of(
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
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
                "ybSoftwareVersion",
                "old-version")),
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
                "commandType",
                KubernetesCommandExecutor.CommandType.HELM_UPGRADE.name(),
                "ybSoftwareVersion",
                "old-version")),
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
                "commandType",
                KubernetesCommandExecutor.CommandType.HELM_UPGRADE.name(),
                "ybSoftwareVersion",
                "old-version")),
        Json.toJson(
            ImmutableMap.of("commandType", KubernetesWaitForPod.CommandType.WAIT_FOR_POD.name())),
        Json.toJson(ImmutableMap.of()),
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
                "old-version")),
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
                "old-version")),
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
                "old-version")),
        Json.toJson(
            ImmutableMap.of("commandType", KubernetesWaitForPod.CommandType.WAIT_FOR_POD.name())),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()));
  }

  private TaskInfo submitTask(RollbackUpgradeParams taskParams) {
    return submitTask(taskParams, TaskType.RollbackKubernetesUpgrade, commissioner);
  }

  @Before
  public void setup() throws Exception {
    setFollowerLagMock();
    setUnderReplicatedTabletsMock();
    setCheckNodesAreSafeToTakeDown(mockClient);
    this.rollbackKubernetesUpgrade =
        new RollbackKubernetesUpgrade(mockBaseTaskDependencies, mockOperatorStatusUpdaterFactory);
    rollbackKubernetesUpgrade.setTaskUUID(UUID.randomUUID());
  }

  @Test
  public void testRollbackUpgradeRetries() {
    setupUniverseSingleAZ(false, true);
    UniverseDefinitionTaskParams.PrevYBSoftwareConfig ybSoftwareConfig =
        new UniverseDefinitionTaskParams.PrevYBSoftwareConfig();
    ybSoftwareConfig.setAutoFlagConfigVersion(1);
    ybSoftwareConfig.setSoftwareVersion("old-version");
    TestHelper.updateUniversePrevSoftwareConfig(defaultUniverse, ybSoftwareConfig);
    TestHelper.updateUniverseIsRollbackAllowed(defaultUniverse, true);
    TestHelper.updateUniverseVersion(defaultUniverse, "new-version");
    RollbackUpgradeParams taskParams = new RollbackUpgradeParams();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = -1;
    taskParams.sleepAfterMasterRestartMillis = 0;
    taskParams.sleepAfterTServerRestartMillis = 0;
    super.verifyTaskRetries(
        defaultCustomer,
        CustomerTask.TaskType.RollbackUpgrade,
        CustomerTask.TargetType.Universe,
        defaultUniverse.getUniverseUUID(),
        TaskType.RollbackKubernetesUpgrade,
        taskParams,
        false);
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertFalse(defaultUniverse.getUniverseDetails().isSoftwareRollbackAllowed);
    assertEquals(
        SoftwareUpgradeState.Ready, defaultUniverse.getUniverseDetails().softwareUpgradeState);
  }

  @Test
  public void testRollbackUpgradeSingleAZ() {
    setupUniverseSingleAZ(false, true);
    UniverseDefinitionTaskParams.PrevYBSoftwareConfig ybSoftwareConfig =
        new UniverseDefinitionTaskParams.PrevYBSoftwareConfig();
    ybSoftwareConfig.setAutoFlagConfigVersion(1);
    ybSoftwareConfig.setSoftwareVersion("old-version");
    TestHelper.updateUniversePrevSoftwareConfig(defaultUniverse, ybSoftwareConfig);
    TestHelper.updateUniverseIsRollbackAllowed(defaultUniverse, true);
    TestHelper.updateUniverseVersion(defaultUniverse, "new-version");

    ArgumentCaptor<String> expectedYbSoftwareVersion = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedPodName = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map<String, String>> expectedConfig = ArgumentCaptor.forClass(Map.class);
    ArgumentCaptor<UUID> expectedUniverseUUID = ArgumentCaptor.forClass(UUID.class);

    String overrideFileRegex = "(.*)" + defaultUniverse.getUniverseUUID() + "(.*).yml";

    RollbackUpgradeParams taskParams = new RollbackUpgradeParams();
    TaskInfo taskInfo = submitTask(taskParams);

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

    assertEquals("old-version", expectedYbSoftwareVersion.getValue());
    assertEquals(config, expectedConfig.getValue());
    assertEquals(NODE_PREFIX, expectedNodePrefix.getValue());
    assertEquals(NODE_PREFIX, expectedNamespace.getValue());
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertTaskSequence(subTasksByPosition, UPGRADE_TASK_SEQUENCE, createUpgradeResult(true));
    assertEquals(Success, taskInfo.getTaskState());
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertFalse(defaultUniverse.getUniverseDetails().isSoftwareRollbackAllowed);
    assertNull(defaultUniverse.getUniverseDetails().prevYBSoftwareConfig);
    assertEquals(
        SoftwareUpgradeState.Ready, defaultUniverse.getUniverseDetails().softwareUpgradeState);
  }

  @Test
  public void testRollbackUpgradeMultiAZ() {
    setupUniverseMultiAZ(false, true);
    UniverseDefinitionTaskParams.PrevYBSoftwareConfig ybSoftwareConfig =
        new UniverseDefinitionTaskParams.PrevYBSoftwareConfig();
    ybSoftwareConfig.setAutoFlagConfigVersion(1);
    ybSoftwareConfig.setSoftwareVersion("old-version");
    TestHelper.updateUniversePrevSoftwareConfig(defaultUniverse, ybSoftwareConfig);
    TestHelper.updateUniverseIsRollbackAllowed(defaultUniverse, true);
    TestHelper.updateUniverseVersion(defaultUniverse, "new-version");

    ArgumentCaptor<String> expectedYbSoftwareVersion = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedPodName = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map<String, String>> expectedConfig = ArgumentCaptor.forClass(Map.class);
    ArgumentCaptor<UUID> expectedUniverseUUID = ArgumentCaptor.forClass(UUID.class);
    String overrideFileRegex = "(.*)" + defaultUniverse.getUniverseUUID() + "(.*).yml";

    RollbackUpgradeParams taskParams = new RollbackUpgradeParams();
    TaskInfo taskInfo = submitTask(taskParams);

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

    assertEquals("old-version", expectedYbSoftwareVersion.getValue());
    assertEquals(config, expectedConfig.getValue());
    assertTrue(expectedNodePrefix.getValue().contains(NODE_PREFIX));
    assertTrue(expectedNamespace.getValue().contains(NODE_PREFIX));
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertTaskSequence(subTasksByPosition, UPGRADE_TASK_SEQUENCE, createUpgradeResult(false));
    assertEquals(Success, taskInfo.getTaskState());
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertFalse(defaultUniverse.getUniverseDetails().isSoftwareRollbackAllowed);
    assertNull(defaultUniverse.getUniverseDetails().prevYBSoftwareConfig);
    assertEquals(
        SoftwareUpgradeState.Ready, defaultUniverse.getUniverseDetails().softwareUpgradeState);
  }
}
