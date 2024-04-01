// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType.DownloadingSoftware;
import static com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType.MASTER;
import static com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType.TSERVER;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.ArgumentMatchers.anyList;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.RunYsqlUpgrade;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.TestHelper;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeOption;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import lombok.extern.slf4j.Slf4j;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.InjectMocks;
import org.mockito.Mockito;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.yb.client.IsInitDbDoneResponse;
import org.yb.client.UpgradeYsqlResponse;

@RunWith(JUnitParamsRunner.class)
@Slf4j
public class SoftwareUpgradeTest extends UpgradeTaskTest {

  @Rule public MockitoRule rule = MockitoJUnit.rule();

  @InjectMocks private SoftwareUpgrade softwareUpgrade;

  private static final String OLD_VERSION = "2.15.0.0-b1";
  private static final String NEW_VERSION = "2.17.0.0-b1";

  private static final List<TaskType> ROLLING_UPGRADE_TASK_SEQUENCE_MASTER =
      ImmutableList.of(
          TaskType.SetNodeState,
          TaskType.CheckNodesAreSafeToTakeDown,
          TaskType.RunHooks,
          TaskType.AnsibleClusterServerCtl,
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.WaitForEncryptionKeyInMemory,
          TaskType.CheckFollowerLag,
          TaskType.RunHooks,
          TaskType.SetNodeState,
          TaskType.WaitStartingFromTime);

  private static final List<TaskType> ROLLING_UPGRADE_TASK_SEQUENCE_MASTER_WITH_YBC_INSTALL =
      ImmutableList.of(
          TaskType.SetNodeState,
          TaskType.CheckNodesAreSafeToTakeDown,
          TaskType.RunHooks,
          TaskType.AnsibleClusterServerCtl,
          TaskType.AnsibleSetupServer,
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.WaitForEncryptionKeyInMemory,
          TaskType.CheckFollowerLag,
          TaskType.RunHooks,
          TaskType.SetNodeState,
          TaskType.WaitStartingFromTime);

  private static final List<TaskType> ROLLING_UPGRADE_TASK_SEQUENCE_TSERVER =
      ImmutableList.of(
          TaskType.SetNodeState,
          TaskType.CheckUnderReplicatedTablets,
          TaskType.CheckNodesAreSafeToTakeDown,
          TaskType.RunHooks,
          TaskType.ModifyBlackList,
          TaskType.WaitForLeaderBlacklistCompletion,
          TaskType.AnsibleClusterServerCtl,
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.WaitForEncryptionKeyInMemory,
          TaskType.ModifyBlackList,
          TaskType.CheckFollowerLag,
          TaskType.RunHooks,
          TaskType.SetNodeState,
          TaskType.WaitStartingFromTime);

  private static final List<TaskType> ROLLING_UPGRADE_TASK_SEQUENCE_TSERVER_WITH_YBC_INSTALL =
      ImmutableList.of(
          TaskType.SetNodeState,
          TaskType.CheckUnderReplicatedTablets,
          TaskType.CheckNodesAreSafeToTakeDown,
          TaskType.RunHooks,
          TaskType.ModifyBlackList,
          TaskType.WaitForLeaderBlacklistCompletion,
          TaskType.AnsibleClusterServerCtl,
          TaskType.AnsibleSetupServer,
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.WaitForEncryptionKeyInMemory,
          TaskType.ModifyBlackList,
          TaskType.CheckFollowerLag,
          TaskType.RunHooks,
          TaskType.SetNodeState,
          TaskType.WaitStartingFromTime);

  private static final List<TaskType> ROLLING_UPGRADE_TASK_SEQUENCE_INACTIVE_ROLE =
      ImmutableList.of(
          TaskType.SetNodeState,
          TaskType.RunHooks,
          TaskType.AnsibleClusterServerCtl,
          TaskType.AnsibleConfigureServers,
          TaskType.RunHooks,
          TaskType.SetNodeState,
          TaskType.WaitStartingFromTime);

  private static final List<TaskType> ROLLING_UPGRADE_TASK_SEQUENCE_INACTIVE_ROLE_WITH_YBC_INSTALL =
      ImmutableList.of(
          TaskType.SetNodeState,
          TaskType.RunHooks,
          TaskType.AnsibleClusterServerCtl,
          TaskType.AnsibleSetupServer,
          TaskType.AnsibleConfigureServers,
          TaskType.RunHooks,
          TaskType.SetNodeState,
          TaskType.WaitStartingFromTime);

  private static final List<TaskType> NON_ROLLING_UPGRADE_TASK_SEQUENCE_ACTIVE_ROLE =
      ImmutableList.of(
          TaskType.SetNodeState,
          TaskType.AnsibleClusterServerCtl,
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.SetNodeState);

  private static final List<TaskType> NON_ROLLING_UPGRADE_TASK_SEQUENCE_INACTIVE_ROLE =
      ImmutableList.of(
          TaskType.SetNodeState,
          TaskType.AnsibleClusterServerCtl,
          TaskType.AnsibleConfigureServers,
          TaskType.SetNodeState);

  @Override
  @Before
  public void setUp() {
    super.setUp();

    attachHooks("SoftwareUpgrade");

    softwareUpgrade.setUserTaskUUID(UUID.randomUUID());
    ShellResponse successResponse = new ShellResponse();
    successResponse.message = "YSQL successfully upgraded to the latest version";

    ShellResponse shellResponse = new ShellResponse();
    shellResponse.message = "Command output:\n2989898";
    shellResponse.code = 0;
    List<String> command = new ArrayList<>();
    command.add("awk");
    command.add(String.format("/%s/ {print$2}", Util.AVAILABLE_MEMORY));
    command.add("/proc/meminfo");
    when(mockNodeUniverseManager.runCommand(any(), any(), eq(command), any()))
        .thenReturn(shellResponse);

    List<String> command2 = new ArrayList<>();
    command2.add("locale");
    command2.add("-a");
    command2.add("|");
    command2.add("grep");
    command2.add("-E");
    command2.add("-q");
    command2.add("\"en_US.utf8|en_US.UTF-8\"");
    command2.add("&&");
    command2.add("echo");
    command2.add("\"Locale is present\"");
    command2.add("||");
    command2.add("echo");
    command2.add("\"Locale is not present\"");
    ShellResponse shellResponse2 = new ShellResponse();
    shellResponse2.message = "Command output:\\nLocale is present";
    shellResponse2.code = 0;
    when(mockNodeUniverseManager.runCommand(any(), any(), eq(command2), any()))
        .thenReturn(shellResponse2);

    try {
      UpgradeYsqlResponse mockUpgradeYsqlResponse = new UpgradeYsqlResponse(1000, "", null);
      when(mockYBClient.getClientWithConfig(any())).thenReturn(mockClient);
      when(mockClient.upgradeYsql(any(HostAndPort.class), anyBoolean()))
          .thenReturn(mockUpgradeYsqlResponse);
      IsInitDbDoneResponse mockIsInitDbDoneResponse =
          new IsInitDbDoneResponse(1000, "", true, true, null, null);
      when(mockClient.getIsInitDbDone()).thenReturn(mockIsInitDbDoneResponse);
      setCheckNodesAreSafeToTakeDown(mockClient);
    } catch (Exception ignored) {
      fail();
    }

    factory
        .forUniverse(defaultUniverse)
        .setValue(RunYsqlUpgrade.USE_SINGLE_CONNECTION_PARAM, "true");

    setUnderReplicatedTabletsMock();
    setFollowerLagMock();
    RuntimeConfigEntry.upsertGlobal("yb.checks.leaderless_tablets.enabled", "false");
  }

  private TaskInfo submitTask(SoftwareUpgradeParams requestParams) {
    return submitTask(requestParams, TaskType.SoftwareUpgrade, commissioner);
  }

  private TaskInfo submitTask(SoftwareUpgradeParams requestParams, int expectedVersion) {
    return submitTask(requestParams, TaskType.SoftwareUpgrade, commissioner, expectedVersion);
  }

  private int assertCommonTasks(
      Map<Integer, List<TaskInfo>> subTasksByPosition,
      int startPosition,
      UpgradeType type,
      boolean isFinalStep,
      boolean systemCatalogUpgrade) {
    return assertCommonTasks(
        subTasksByPosition, startPosition, type, isFinalStep, systemCatalogUpgrade, false);
  }

  private int assertCommonTasks(
      Map<Integer, List<TaskInfo>> subTasksByPosition,
      int startPosition,
      UpgradeType type,
      boolean isFinalStep,
      boolean systemCatalogUpgrade,
      boolean installYbc) {
    int position = startPosition;
    List<TaskType> commonNodeTasks = new ArrayList<>();

    if (type.name().equals("ROLLING_UPGRADE_TSERVER_ONLY") && !isFinalStep) {
      commonNodeTasks.add(TaskType.ModifyBlackList);
    }

    if (installYbc) {
      commonNodeTasks.add(TaskType.AnsibleConfigureServers);
      commonNodeTasks.add(TaskType.WaitForYbcServer);
      commonNodeTasks.add(TaskType.UpdateUniverseYbcDetails);
    }

    if (isFinalStep) {
      commonNodeTasks.addAll(
          ImmutableList.of(TaskType.CheckSoftwareVersion, TaskType.PromoteAutoFlags));
      if (systemCatalogUpgrade) {
        commonNodeTasks.add(TaskType.RunYsqlUpgrade);
      }
      commonNodeTasks.addAll(
          ImmutableList.of(
              TaskType.UpdateSoftwareVersion,
              TaskType.UpdateUniverseState,
              TaskType.RunHooks,
              TaskType.UniverseUpdateSucceeded));
    }
    for (TaskType commonNodeTask : commonNodeTasks) {
      assertTaskType(subTasksByPosition.get(position), commonNodeTask);
      position++;
    }
    return position;
  }

  private int assertSequence(
      Map<Integer, List<TaskInfo>> subTasksByPosition,
      ServerType serverType,
      int startPosition,
      boolean isRollingUpgrade,
      boolean activeRole) {
    return assertSequence(
        subTasksByPosition, serverType, startPosition, isRollingUpgrade, activeRole, false);
  }

  private int assertSequence(
      Map<Integer, List<TaskInfo>> subTasksByPosition,
      ServerType serverType,
      int startPosition,
      boolean isRollingUpgrade,
      boolean activeRole,
      boolean installYbc) {
    int position = startPosition;
    if (isRollingUpgrade) {
      List<TaskType> taskSequence =
          serverType == MASTER
              ? (activeRole
                  ? (installYbc
                      ? ROLLING_UPGRADE_TASK_SEQUENCE_MASTER_WITH_YBC_INSTALL
                      : ROLLING_UPGRADE_TASK_SEQUENCE_MASTER)
                  : (installYbc
                      ? ROLLING_UPGRADE_TASK_SEQUENCE_INACTIVE_ROLE_WITH_YBC_INSTALL
                      : ROLLING_UPGRADE_TASK_SEQUENCE_INACTIVE_ROLE))
              : (installYbc
                  ? ROLLING_UPGRADE_TASK_SEQUENCE_TSERVER_WITH_YBC_INSTALL
                  : ROLLING_UPGRADE_TASK_SEQUENCE_TSERVER);
      List<Integer> nodeOrder = getRollingUpgradeNodeOrder(serverType, activeRole);

      for (int nodeIdx : nodeOrder) {
        String nodeName = String.format("host-n%d", nodeIdx);
        for (TaskType type : taskSequence) {
          List<TaskInfo> tasks = subTasksByPosition.get(position);
          TaskType taskType = tasks.get(0).getTaskType();
          UserTaskDetails.SubTaskGroupType subTaskGroupType = tasks.get(0).getSubTaskGroupType();
          // Leader blacklisting adds a ModifyBlackList task at position 0
          int numTasksToAssert = position == 0 ? 2 : 1;
          assertEquals(numTasksToAssert, tasks.size());
          assertEquals(type, taskType);
          if (!NON_NODE_TASKS.contains(taskType)) {
            Map<String, Object> assertValues =
                new HashMap<>(ImmutableMap.of("nodeName", nodeName, "nodeCount", 1));

            if (taskType.equals(TaskType.AnsibleConfigureServers)) {
              String version = NEW_VERSION;
              String taskSubType =
                  subTaskGroupType.equals(DownloadingSoftware) ? "Download" : "Install";
              assertValues.putAll(
                  ImmutableMap.of(
                      "ybSoftwareVersion", version,
                      "processType", serverType.toString(),
                      "taskSubType", taskSubType));
            }
            assertNodeSubTask(tasks, assertValues);
          }
          position++;
        }
      }
    } else {
      List<TaskType> taskSequence =
          activeRole
              ? NON_ROLLING_UPGRADE_TASK_SEQUENCE_ACTIVE_ROLE
              : NON_ROLLING_UPGRADE_TASK_SEQUENCE_INACTIVE_ROLE;
      for (TaskType type : taskSequence) {
        List<TaskInfo> tasks = subTasksByPosition.get(position);
        TaskType taskType = assertTaskType(tasks, type);

        if (NON_NODE_TASKS.contains(taskType)) {
          assertEquals(1, tasks.size());
        } else {
          List<String> nodes = null;
          // We have 3 nodes as active masters, 2 as inactive masters and 5 tasks/nodes
          // for Tserver.
          if (serverType == MASTER) {
            if (activeRole) {
              nodes = ImmutableList.of("host-n1", "host-n2", "host-n3");
            } else {
              nodes = ImmutableList.of("host-n4", "host-n5");
            }
          } else {
            nodes = ImmutableList.of("host-n1", "host-n2", "host-n3", "host-n4", "host-n5");
          }

          Map<String, Object> assertValues =
              new HashMap<>(ImmutableMap.of("nodeNames", nodes, "nodeCount", nodes.size()));
          if (taskType.equals(TaskType.AnsibleConfigureServers)) {
            String version = NEW_VERSION;
            assertValues.putAll(
                ImmutableMap.of(
                    "ybSoftwareVersion", version, "processType", serverType.toString()));
          }
          assertEquals(nodes.size(), tasks.size());
          assertNodeSubTask(tasks, assertValues);
        }
        position++;
      }
    }
    return position;
  }

  @Test
  public void testSoftwareUpgradeWithSameVersion() {
    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = "2.14.12.0-b1";
    taskParams.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());

    assertThrows(RuntimeException.class, () -> submitTask(taskParams));
    verify(mockNodeManager, times(0)).nodeCommand(any(), any());
    defaultUniverse.refresh();
    assertEquals(2, defaultUniverse.getVersion());
  }

  @Test
  public void testSoftwareUpgradeWithoutVersion() {
    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());
    assertThrows(RuntimeException.class, () -> submitTask(taskParams));
    verify(mockNodeManager, times(0)).nodeCommand(any(), any());
    defaultUniverse.refresh();
    assertEquals(2, defaultUniverse.getVersion());
  }

  @Test
  public void testSoftwareUpgrade() {
    updateDefaultUniverseTo5Nodes(true, OLD_VERSION);

    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = NEW_VERSION;
    taskParams.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());
    mockDBServerVersion(
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
        taskParams.ybSoftwareVersion,
        defaultUniverse.getMasters().size() + defaultUniverse.getTServers().size());
    TaskInfo taskInfo = submitTask(taskParams, defaultUniverse.getVersion());
    verify(mockNodeManager, times(71)).nodeCommand(any(), any());
    verify(mockNodeUniverseManager, times(10)).runCommand(any(), any(), anyList(), any());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    int position = 0;
    assertTaskType(subTasksByPosition.get(position++), TaskType.CheckNodesAreSafeToTakeDown);
    assertTaskType(subTasksByPosition.get(position++), TaskType.CheckUpgrade);
    assertTaskType(subTasksByPosition.get(position++), TaskType.CheckMemory);
    assertTaskType(subTasksByPosition.get(position++), TaskType.CheckLocale);
    assertTaskType(subTasksByPosition.get(position++), TaskType.CheckGlibc);
    assertTaskType(subTasksByPosition.get(position++), TaskType.FreezeUniverse);
    assertTaskType(subTasksByPosition.get(position++), TaskType.RunHooks);
    assertTaskType(subTasksByPosition.get(position++), TaskType.UpdateUniverseState);
    // XCluster gflag set up.
    assertTaskType(subTasksByPosition.get(position++), TaskType.AnsibleConfigureServers);
    assertTaskType(subTasksByPosition.get(position++), TaskType.AnsibleConfigureServers);
    assertTaskType(subTasksByPosition.get(position++), TaskType.XClusterInfoPersist);

    List<TaskInfo> downloadTasks = subTasksByPosition.get(position++);
    assertTaskType(downloadTasks, TaskType.AnsibleConfigureServers);
    assertEquals(5, downloadTasks.size());
    position = assertSequence(subTasksByPosition, MASTER, position, true, false);
    position = assertSequence(subTasksByPosition, MASTER, position, true, true);
    assertTaskType(subTasksByPosition.get(position++), TaskType.ModifyBlackList);
    position =
        assertCommonTasks(subTasksByPosition, position, UpgradeType.ROLLING_UPGRADE, false, true);
    position = assertSequence(subTasksByPosition, TSERVER, position, true, true);
    assertCommonTasks(subTasksByPosition, position, UpgradeType.ROLLING_UPGRADE, true, true);
    assertEquals(151, position);
    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());
  }

  @Test
  public void testSoftwareUpgradeAndInstallYbc() {
    updateDefaultUniverseTo5Nodes(true, OLD_VERSION);
    TestHelper.updateUniverseSystemdDetails(defaultUniverse);

    Mockito.doNothing().when(mockYbcManager).waitForYbc(any(), any());

    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = NEW_VERSION;
    taskParams.installYbc = true;
    taskParams.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());
    mockDBServerVersion(
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
        taskParams.ybSoftwareVersion,
        defaultUniverse.getMasters().size() + defaultUniverse.getTServers().size());
    TaskInfo taskInfo = submitTask(taskParams, defaultUniverse.getVersion());
    verify(mockNodeManager, times(86)).nodeCommand(any(), any());
    verify(mockNodeUniverseManager, times(10)).runCommand(any(), any(), anyList(), any());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    int position = 0;
    assertTaskType(subTasksByPosition.get(position++), TaskType.CheckNodesAreSafeToTakeDown);
    assertTaskType(subTasksByPosition.get(position++), TaskType.CheckUpgrade);
    assertTaskType(subTasksByPosition.get(position++), TaskType.CheckMemory);
    assertTaskType(subTasksByPosition.get(position++), TaskType.CheckLocale);
    assertTaskType(subTasksByPosition.get(position++), TaskType.CheckGlibc);
    assertTaskType(subTasksByPosition.get(position++), TaskType.FreezeUniverse);
    assertTaskType(subTasksByPosition.get(position++), TaskType.RunHooks);
    assertTaskType(subTasksByPosition.get(position++), TaskType.UpdateUniverseState);
    // XCluster gflag set up.
    assertTaskType(subTasksByPosition.get(position++), TaskType.AnsibleConfigureServers);
    assertTaskType(subTasksByPosition.get(position++), TaskType.AnsibleConfigureServers);
    assertTaskType(subTasksByPosition.get(position++), TaskType.XClusterInfoPersist);

    List<TaskInfo> downloadTasks = subTasksByPosition.get(position++);
    assertTaskType(downloadTasks, TaskType.AnsibleConfigureServers);
    assertEquals(5, downloadTasks.size());
    position = assertSequence(subTasksByPosition, MASTER, position, true, false, true);
    position = assertSequence(subTasksByPosition, MASTER, position, true, true, true);
    assertTaskType(subTasksByPosition.get(position++), TaskType.ModifyBlackList);
    position =
        assertCommonTasks(subTasksByPosition, position, UpgradeType.ROLLING_UPGRADE, false, true);
    position = assertSequence(subTasksByPosition, TSERVER, position, true, true, true);
    assertCommonTasks(subTasksByPosition, position, UpgradeType.ROLLING_UPGRADE, true, true, true);
    assertEquals(161, position);
    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());
  }

  @Test
  public void testSoftwareUpgradeAndPromoteAutoFlagsOnOthers() {
    updateDefaultUniverseTo5Nodes(true, OLD_VERSION);

    Universe xClusterUniv = ModelFactory.createUniverse("univ-2");
    XClusterConfig.create(
        "test-2", defaultUniverse.getUniverseUUID(), xClusterUniv.getUniverseUUID());
    Universe xClusterUniv2 = ModelFactory.createUniverse("univ-3");
    XClusterConfig.create(
        "test-3", xClusterUniv.getUniverseUUID(), xClusterUniv2.getUniverseUUID());

    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = NEW_VERSION;
    taskParams.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());
    mockDBServerVersion(
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
        taskParams.ybSoftwareVersion,
        defaultUniverse.getMasters().size() + defaultUniverse.getTServers().size());
    TaskInfo taskInfo = submitTask(taskParams, defaultUniverse.getVersion());
    verify(mockNodeManager, times(71)).nodeCommand(any(), any());
    verify(mockNodeUniverseManager, times(10)).runCommand(any(), any(), anyList(), any());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    assertEquals(
        3,
        subTasks.stream()
            .filter(task -> task.getTaskType().equals(TaskType.PromoteAutoFlags))
            .count());
    assertEquals(Success, taskInfo.getTaskState());
  }

  @Test
  public void testSoftwareUpgradeNoSystemCatalogUpgrade() {
    updateDefaultUniverseTo5Nodes(true, OLD_VERSION);

    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = NEW_VERSION;
    taskParams.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());
    taskParams.upgradeSystemCatalog = false;
    mockDBServerVersion(
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
        taskParams.ybSoftwareVersion,
        defaultUniverse.getMasters().size() + defaultUniverse.getTServers().size());
    TaskInfo taskInfo = submitTask(taskParams, defaultUniverse.getVersion());
    verify(mockNodeManager, times(71)).nodeCommand(any(), any());
    verify(mockNodeUniverseManager, times(10)).runCommand(any(), any(), anyList(), any());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    int position = 0;
    assertTaskType(subTasksByPosition.get(position++), TaskType.CheckNodesAreSafeToTakeDown);
    assertTaskType(subTasksByPosition.get(position++), TaskType.CheckUpgrade);
    assertTaskType(subTasksByPosition.get(position++), TaskType.CheckMemory);
    assertTaskType(subTasksByPosition.get(position++), TaskType.CheckLocale);
    assertTaskType(subTasksByPosition.get(position++), TaskType.CheckGlibc);
    assertTaskType(subTasksByPosition.get(position++), TaskType.FreezeUniverse);
    assertTaskType(subTasksByPosition.get(position++), TaskType.RunHooks);
    assertTaskType(subTasksByPosition.get(position++), TaskType.UpdateUniverseState);
    // XCluster gflag set up.
    assertTaskType(subTasksByPosition.get(position++), TaskType.AnsibleConfigureServers);
    assertTaskType(subTasksByPosition.get(position++), TaskType.AnsibleConfigureServers);
    assertTaskType(subTasksByPosition.get(position++), TaskType.XClusterInfoPersist);

    List<TaskInfo> downloadTasks = subTasksByPosition.get(position++);
    assertTaskType(downloadTasks, TaskType.AnsibleConfigureServers);
    assertEquals(5, downloadTasks.size());
    position = assertSequence(subTasksByPosition, MASTER, position, true, false);
    position = assertSequence(subTasksByPosition, MASTER, position, true, true);
    assertTaskType(subTasksByPosition.get(position++), TaskType.ModifyBlackList);
    position =
        assertCommonTasks(subTasksByPosition, position, UpgradeType.ROLLING_UPGRADE, false, false);
    position = assertSequence(subTasksByPosition, TSERVER, position, true, true);
    assertCommonTasks(subTasksByPosition, position, UpgradeType.ROLLING_UPGRADE, true, false);
    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());
  }

  @Test
  @Parameters({"false", "true"})
  public void testSoftwareUpgradeWithReadReplica(boolean enableYSQL) {
    updateDefaultUniverseTo5Nodes(enableYSQL, OLD_VERSION);

    // Adding Read Replica cluster.
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.numNodes = 3;
    userIntent.replicationFactor = 3;
    userIntent.ybSoftwareVersion = OLD_VERSION;
    userIntent.accessKeyCode = "demo-access";
    userIntent.regionList = ImmutableList.of(region.getUuid());
    userIntent.enableYSQL = enableYSQL;
    userIntent.provider = defaultProvider.getUuid().toString();

    PlacementInfo pi = new PlacementInfo();
    AvailabilityZone az4 = AvailabilityZone.createOrThrow(region, "az-4", "AZ 4", "subnet-1");
    AvailabilityZone az5 = AvailabilityZone.createOrThrow(region, "az-5", "AZ 5", "subnet-2");
    AvailabilityZone az6 = AvailabilityZone.createOrThrow(region, "az-6", "AZ 6", "subnet-3");

    // Currently read replica zones are always affinitized.
    PlacementInfoUtil.addPlacementZone(az4.getUuid(), pi, 1, 1, false);
    PlacementInfoUtil.addPlacementZone(az5.getUuid(), pi, 1, 1, true);
    PlacementInfoUtil.addPlacementZone(az6.getUuid(), pi, 1, 1, false);

    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            ApiUtils.mockUniverseUpdaterWithReadReplica(userIntent, pi));

    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = NEW_VERSION;
    taskParams.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());
    mockDBServerVersion(
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
        taskParams.ybSoftwareVersion,
        defaultUniverse.getMasters().size() + defaultUniverse.getTServers().size());
    TaskInfo taskInfo = submitTask(taskParams, defaultUniverse.getVersion());
    verify(mockNodeManager, times(95)).nodeCommand(any(), any());
    verify(mockNodeUniverseManager, times(16)).runCommand(any(), any(), anyList(), any());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    int position = 0;
    assertTaskType(subTasksByPosition.get(position++), TaskType.CheckNodesAreSafeToTakeDown);
    assertTaskType(subTasksByPosition.get(position++), TaskType.CheckUpgrade);
    assertTaskType(subTasksByPosition.get(position++), TaskType.CheckMemory);
    assertTaskType(subTasksByPosition.get(position++), TaskType.CheckLocale);
    assertTaskType(subTasksByPosition.get(position++), TaskType.CheckGlibc);
    assertTaskType(subTasksByPosition.get(position++), TaskType.FreezeUniverse);
    assertTaskType(subTasksByPosition.get(position++), TaskType.RunHooks);
    assertTaskType(subTasksByPosition.get(position++), TaskType.UpdateUniverseState);
    // XCluster gflag set up.
    assertTaskType(subTasksByPosition.get(position++), TaskType.AnsibleConfigureServers);
    assertTaskType(subTasksByPosition.get(position++), TaskType.AnsibleConfigureServers);
    assertTaskType(subTasksByPosition.get(position++), TaskType.XClusterInfoPersist);

    List<TaskInfo> downloadTasks = subTasksByPosition.get(position++);
    assertTaskType(downloadTasks, TaskType.AnsibleConfigureServers);
    assertEquals(8, downloadTasks.size());
    position = assertSequence(subTasksByPosition, MASTER, position, true, false);
    position = assertSequence(subTasksByPosition, MASTER, position, true, true);
    assertTaskType(subTasksByPosition.get(position++), TaskType.ModifyBlackList);
    position =
        assertCommonTasks(subTasksByPosition, position, UpgradeType.ROLLING_UPGRADE, false, true);
    position = assertSequence(subTasksByPosition, TSERVER, position, true, true);
    assertCommonTasks(subTasksByPosition, position, UpgradeType.ROLLING_UPGRADE, true, true);
    assertEquals(202, position);
    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());
  }

  @Test
  public void testSoftwareNonRollingUpgrade() {
    updateDefaultUniverseTo5Nodes(true, OLD_VERSION);

    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = NEW_VERSION;
    taskParams.upgradeOption = UpgradeOption.NON_ROLLING_UPGRADE;
    taskParams.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());

    mockDBServerVersion(
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
        taskParams.ybSoftwareVersion,
        defaultUniverse.getMasters().size() + defaultUniverse.getTServers().size());

    TaskInfo taskInfo = submitTask(taskParams, defaultUniverse.getVersion());
    ArgumentCaptor<NodeTaskParams> commandParams = ArgumentCaptor.forClass(NodeTaskParams.class);
    verify(mockNodeManager, times(51)).nodeCommand(any(), commandParams.capture());
    verify(mockNodeUniverseManager, times(10)).runCommand(any(), any(), anyList(), any());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    int position = 0;
    assertTaskType(subTasksByPosition.get(position++), TaskType.CheckUpgrade);
    assertTaskType(subTasksByPosition.get(position++), TaskType.CheckMemory);
    assertTaskType(subTasksByPosition.get(position++), TaskType.CheckLocale);
    assertTaskType(subTasksByPosition.get(position++), TaskType.CheckGlibc);
    assertTaskType(subTasksByPosition.get(position++), TaskType.FreezeUniverse);
    assertTaskType(subTasksByPosition.get(position++), TaskType.RunHooks);
    assertTaskType(subTasksByPosition.get(position++), TaskType.UpdateUniverseState);
    // XCluster gflag set up.
    assertTaskType(subTasksByPosition.get(position++), TaskType.AnsibleConfigureServers);
    assertTaskType(subTasksByPosition.get(position++), TaskType.AnsibleConfigureServers);
    assertTaskType(subTasksByPosition.get(position++), TaskType.XClusterInfoPersist);

    List<TaskInfo> downloadTasks = subTasksByPosition.get(position++);
    assertTaskType(downloadTasks, TaskType.AnsibleConfigureServers);
    assertEquals(5, downloadTasks.size());
    position = assertSequence(subTasksByPosition, MASTER, position, false, false);
    position = assertSequence(subTasksByPosition, MASTER, position, false, true);
    position = assertSequence(subTasksByPosition, TSERVER, position, false, true);
    assertCommonTasks(subTasksByPosition, position, UpgradeType.FULL_UPGRADE, true, true);
    assertEquals(27, position);
    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());
  }

  protected List<Integer> getRollingUpgradeNodeOrder(ServerType serverType, boolean activeRole) {
    return serverType == MASTER
        ?
        // We need to check that the master leader is upgraded last.
        (activeRole ? Arrays.asList(1, 3, 2) : Arrays.asList(4, 5))
        :
        // We need to check that isAffinitized zone node is upgraded getFirst().
        defaultUniverse.getUniverseDetails().getReadOnlyClusters().isEmpty()
            ? Arrays.asList(3, 1, 2, 4, 5)
            :
            // Primary cluster getFirst(), then read replica.
            Arrays.asList(3, 1, 2, 4, 5, 8, 6, 7);
  }

  @Test
  public void testSoftwareUpgradeRetries() {
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        univ -> {
          UniverseDefinitionTaskParams details = univ.getUniverseDetails();
          details.getPrimaryCluster().userIntent.ybSoftwareVersion = OLD_VERSION;
          univ.setUniverseDetails(details);
        });
    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = NEW_VERSION;
    taskParams.expectedUniverseVersion = -1;
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());
    taskParams.creatingUser = defaultUser;
    TestUtils.setFakeHttpContext(defaultUser);
    super.verifyTaskRetries(
        defaultCustomer,
        CustomerTask.TaskType.SoftwareUpgrade,
        CustomerTask.TargetType.Universe,
        defaultUniverse.getUniverseUUID(),
        TaskType.SoftwareUpgrade,
        taskParams,
        false);
  }

  @Test
  public void testPartialSoftwareUpgrade() {
    updateDefaultUniverseTo5Nodes(true, OLD_VERSION);

    Set<String> mastersOriginallyUpdated = new HashSet<>();
    Set<String> tserversOriginallyUpdated = new HashSet<>();

    List<NodeDetails> masters = defaultUniverse.getMasters();
    NodeDetails onlyMasterUpdated = masters.get(0);
    mastersOriginallyUpdated.add(onlyMasterUpdated.cloudInfo.private_ip);
    NodeDetails bothUpdated = masters.get(1);
    mastersOriginallyUpdated.add(bothUpdated.cloudInfo.private_ip);
    tserversOriginallyUpdated.add(bothUpdated.cloudInfo.private_ip);

    List<NodeDetails> otherTservers =
        defaultUniverse.getTServers().stream()
            .filter(n -> !masters.contains(n))
            .collect(Collectors.toList());

    NodeDetails tserverUpdated = otherTservers.get(0);
    tserversOriginallyUpdated.add(tserverUpdated.cloudInfo.private_ip);

    NodeDetails tserverUpdatedButNotLive = otherTservers.get(1);
    tserversOriginallyUpdated.add(tserverUpdatedButNotLive.cloudInfo.private_ip);

    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            u -> {
              UniverseDefinitionTaskParams details = u.getUniverseDetails();
              u.getNode(tserverUpdatedButNotLive.getNodeName()).state =
                  NodeDetails.NodeState.UpgradeSoftware;
              u.setUniverseDetails(details);
            });

    Set<String> mastersUpdated = new HashSet<>(mastersOriginallyUpdated);
    Set<String> tserversUpdated = new HashSet<>(tserversOriginallyUpdated);

    when(mockYBClient.getServerVersion(any(), anyString(), anyInt()))
        .thenAnswer(
            invocation -> {
              String ip = invocation.getArgument(1);
              int port = invocation.getArgument(2);
              boolean isMaster = port == 7100;
              Set<String> serversUpdated = isMaster ? mastersUpdated : tserversUpdated;
              Optional<String> result =
                  serversUpdated.add(ip) ? Optional.of(OLD_VERSION) : Optional.of(NEW_VERSION);
              NodeDetails node = defaultUniverse.getNodeByPrivateIP(ip);
              return result;
            });

    defaultUniverse.refresh();
    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = NEW_VERSION;
    taskParams.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());

    TaskInfo taskInfo = submitTask(taskParams, defaultUniverse.getVersion());
    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());

    Set<String> configuredMasters =
        taskInfo.getSubTasks().stream()
            .filter(t -> t.getTaskType() == TaskType.AnsibleConfigureServers)
            .filter(t -> t.getTaskParams().get("type").asText().equals("Software"))
            .filter(
                t ->
                    t.getTaskParams()
                        .get("properties")
                        .get("processType")
                        .asText()
                        .equals("MASTER"))
            .map(t -> t.getTaskParams().get("nodeName").asText())
            .collect(Collectors.toSet());

    Set<String> configuredTservers =
        taskInfo.getSubTasks().stream()
            .filter(t -> t.getTaskType() == TaskType.AnsibleConfigureServers)
            .filter(t -> t.getTaskParams().get("type").asText().equals("Software"))
            .filter(
                t ->
                    t.getTaskParams()
                        .get("properties")
                        .get("processType")
                        .asText()
                        .equals("TSERVER"))
            .map(t -> t.getTaskParams().get("nodeName").asText())
            .collect(Collectors.toSet());

    Set<String> expectedMasters =
        defaultUniverse.getMasters().stream()
            .filter(n -> !mastersOriginallyUpdated.contains(n.cloudInfo.private_ip))
            .map(n -> n.nodeName)
            .collect(Collectors.toSet());
    Set<String> expectedTservers =
        defaultUniverse.getTServers().stream()
            .filter(
                n ->
                    !tserversOriginallyUpdated.contains(n.cloudInfo.private_ip)
                        || n.nodeName.equals(tserverUpdatedButNotLive.nodeName))
            .map(n -> n.nodeName)
            .collect(Collectors.toSet());

    // We do process inactive masters, so for each tserver we also process masters
    expectedMasters.addAll(expectedTservers);

    assertEquals("Upgraded masters", expectedMasters, configuredMasters);
    assertEquals("Upgraded tservers", expectedTservers, configuredTservers);
  }
}
