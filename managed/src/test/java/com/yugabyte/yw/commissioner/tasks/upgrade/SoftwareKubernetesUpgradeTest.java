// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
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
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.RegexMatcher;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.ArrayList;
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
import org.yb.client.IsInitDbDoneResponse;
import org.yb.client.UpgradeYsqlResponse;
import org.yb.client.YBClient;
import play.libs.Json;

public class SoftwareKubernetesUpgradeTest extends KubernetesUpgradeTaskTest {

  @Rule public MockitoRule rule = MockitoJUnit.rule();

  private SoftwareKubernetesUpgrade softwareKubernetesUpgrade;

  private YBClient mockClient;

  private static final List<TaskType> UPGRADE_TASK_SEQUENCE =
      ImmutableList.of(
          TaskType.CheckUpgrade,
          TaskType.CheckLeaderlessTablets,
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
          TaskType.ModifyBlackList,
          TaskType.CheckUnderReplicatedTablets,
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
          TaskType.PromoteAutoFlags,
          TaskType.RunYsqlUpgrade,
          TaskType.UpdateSoftwareVersion,
          TaskType.UniverseUpdateSucceeded,
          TaskType.ModifyBlackList);

  @Before
  public void setUp() {
    super.setUp();
    when(mockOperatorStatusUpdaterFactory.create()).thenReturn(mockOperatorStatusUpdater);
    this.softwareKubernetesUpgrade =
        new SoftwareKubernetesUpgrade(
            mockBaseTaskDependencies, null, mockOperatorStatusUpdaterFactory);
    UpgradeYsqlResponse mockUpgradeYsqlResponse = new UpgradeYsqlResponse(1000, "", null);
    IsInitDbDoneResponse mockIsInitDbDoneResponse =
        new IsInitDbDoneResponse(1000, "", true, true, null, null);
    mockClient = mock(YBClient.class);
    when(mockYBClient.getClientWithConfig(any())).thenReturn(mockClient);

    try {
      when(mockClient.upgradeYsql(any(HostAndPort.class), anyBoolean()))
          .thenReturn(mockUpgradeYsqlResponse);
      when(mockClient.getIsInitDbDone()).thenReturn(mockIsInitDbDoneResponse);
    } catch (Exception ignored) {
    }
    setUnderReplicatedTabletsMock();
    setFollowerLagMock();
    setLeaderlessTabletsMock();
    when(mockClient.getLeaderMasterHostAndPort()).thenReturn(HostAndPort.fromHost("10.0.0.1"));
  }

  private TaskInfo submitTask(SoftwareUpgradeParams taskParams) {
    return submitTask(taskParams, TaskType.SoftwareKubernetesUpgrade, commissioner);
  }

  private static List<JsonNode> createUpgradeResult(boolean isSingleAZ) {
    String namespace = isSingleAZ ? "demo-universe" : "demo-universe-az-2";
    return ImmutableList.of(
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(
            ImmutableMap.of("commandType", KubernetesCommandExecutor.CommandType.POD_INFO.name())),
        Json.toJson(
            ImmutableMap.of(
                "commandType",
                KubernetesCommandExecutor.CommandType.HELM_UPGRADE.name(),
                "ybSoftwareVersion",
                YB_SOFTWARE_VERSION_NEW)),
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
                YB_SOFTWARE_VERSION_NEW)),
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
                YB_SOFTWARE_VERSION_NEW)),
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
        Json.toJson(
            ImmutableMap.of(
                "commandType",
                KubernetesCommandExecutor.CommandType.HELM_UPGRADE.name(),
                "ybSoftwareVersion",
                YB_SOFTWARE_VERSION_NEW,
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
        Json.toJson(
            ImmutableMap.of(
                "commandType",
                KubernetesCommandExecutor.CommandType.HELM_UPGRADE.name(),
                "ybSoftwareVersion",
                YB_SOFTWARE_VERSION_NEW)),
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
        Json.toJson(
            ImmutableMap.of(
                "commandType",
                KubernetesCommandExecutor.CommandType.HELM_UPGRADE.name(),
                "ybSoftwareVersion",
                YB_SOFTWARE_VERSION_NEW)),
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
        Json.toJson(ImmutableMap.of()));
  }

  @Test
  public void testSoftwareUpgradeSingleAZ() {
    softwareKubernetesUpgrade.setUserTaskUUID(UUID.randomUUID());
    setupUniverseSingleAZ(false, true);

    ArgumentCaptor<String> expectedYbSoftwareVersion = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedPodName = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map<String, String>> expectedConfig = ArgumentCaptor.forClass(Map.class);
    ArgumentCaptor<UUID> expectedUniverseUUID = ArgumentCaptor.forClass(UUID.class);

    String overrideFileRegex = "(.*)" + defaultUniverse.getUniverseUUID() + "(.*).yml";

    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = YB_SOFTWARE_VERSION_NEW;
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

    assertEquals(YB_SOFTWARE_VERSION_NEW, expectedYbSoftwareVersion.getValue());
    assertEquals(config, expectedConfig.getValue());
    assertEquals(NODE_PREFIX, expectedNodePrefix.getValue());
    assertEquals(NODE_PREFIX, expectedNamespace.getValue());
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertTaskSequence(subTasksByPosition, UPGRADE_TASK_SEQUENCE, createUpgradeResult(true));
    assertEquals(Success, taskInfo.getTaskState());
  }

  @Test
  public void testSoftwareUpgradeMultiAZ() {
    softwareKubernetesUpgrade.setUserTaskUUID(UUID.randomUUID());
    setupUniverseMultiAZ(false, true);

    ArgumentCaptor<String> expectedYbSoftwareVersion = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedPodName = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map<String, String>> expectedConfig = ArgumentCaptor.forClass(Map.class);
    ArgumentCaptor<UUID> expectedUniverseUUID = ArgumentCaptor.forClass(UUID.class);
    String overrideFileRegex = "(.*)" + defaultUniverse.getUniverseUUID() + "(.*).yml";

    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = YB_SOFTWARE_VERSION_NEW;
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

    assertEquals(YB_SOFTWARE_VERSION_NEW, expectedYbSoftwareVersion.getValue());
    assertEquals(config, expectedConfig.getValue());
    assertTrue(expectedNodePrefix.getValue().contains(NODE_PREFIX));
    assertTrue(expectedNamespace.getValue().contains(NODE_PREFIX));
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertTaskSequence(subTasksByPosition, UPGRADE_TASK_SEQUENCE, createUpgradeResult(false));
    assertEquals(Success, taskInfo.getTaskState());
  }

  @Test
  public void testSoftwareUpgradeNoSystemCatalogUpgrade() {
    softwareKubernetesUpgrade.setUserTaskUUID(UUID.randomUUID());
    setupUniverseSingleAZ(false, true);

    String overrideFileRegex = "(.*)" + defaultUniverse.getUniverseUUID() + "(.*).yml";

    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = YB_SOFTWARE_VERSION_NEW;
    taskParams.upgradeSystemCatalog = false;
    TaskInfo taskInfo = submitTask(taskParams);

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    List<TaskType> expectedTasks = new ArrayList<>(UPGRADE_TASK_SEQUENCE);
    expectedTasks.remove(TaskType.RunYsqlUpgrade);
    assertTaskSequence(subTasksByPosition, expectedTasks, createUpgradeResult(true));
    assertEquals(Success, taskInfo.getTaskState());
  }

  @Test
  public void testSoftwareUpgradeAndPromoteAutoFlagsOnOthers() {
    softwareKubernetesUpgrade.setUserTaskUUID(UUID.randomUUID());
    setupUniverseSingleAZ(false, true);

    Universe xClusterUniv = ModelFactory.createUniverse("univ-2");
    XClusterConfig.create(
        "test-2", defaultUniverse.getUniverseUUID(), xClusterUniv.getUniverseUUID());

    String overrideFileRegex = "(.*)" + defaultUniverse.getUniverseUUID() + "(.*).yml";

    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = YB_SOFTWARE_VERSION_NEW;
    TaskInfo taskInfo = submitTask(taskParams);

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    List<TaskType> expectedTasks = new ArrayList<>(UPGRADE_TASK_SEQUENCE);
    expectedTasks.add(
        expectedTasks.lastIndexOf(TaskType.PromoteAutoFlags), TaskType.PromoteAutoFlags);
    List<JsonNode> expectedResultLists = new ArrayList<>(createUpgradeResult(true));
    expectedResultLists.add(Json.toJson(ImmutableMap.of()));
    assertTaskSequence(subTasksByPosition, expectedTasks, expectedResultLists);
    assertEquals(Success, taskInfo.getTaskState());
  }

  @Test
  public void testSoftwareKubernetesUpgradeRetries() {
    softwareKubernetesUpgrade.setUserTaskUUID(UUID.randomUUID());
    setupUniverseSingleAZ(false, true);
    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = YB_SOFTWARE_VERSION_NEW;

    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = 2;
    super.verifyTaskRetries(
        defaultCustomer,
        CustomerTask.TaskType.SoftwareUpgrade,
        CustomerTask.TargetType.Universe,
        defaultUniverse.getUniverseUUID(),
        TaskType.SoftwareKubernetesUpgrade,
        taskParams);
  }
}
