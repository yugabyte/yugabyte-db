// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType.DownloadingSoftware;
import static com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType.MASTER;
import static com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType.TSERVER;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyList;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.*;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.MockUpgrade;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.TestHelper;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.RollbackUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.PrevYBSoftwareConfig;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.SoftwareUpgradeState;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeOption;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.InstanceType.InstanceTypeDetails;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import junitparams.JUnitParamsRunner;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.yb.client.GetYsqlMajorCatalogUpgradeStateResponse;
import org.yb.master.MasterAdminOuterClass.YsqlMajorCatalogUpgradeState;

@RunWith(JUnitParamsRunner.class)
@Slf4j
public class RollbackUpgradeTest extends UpgradeTaskTest {

  @Rule public MockitoRule rule = MockitoJUnit.rule();

  @InjectMocks private RollbackUpgrade rollbackUpgrade;

  private static final List<TaskType> ROLLING_UPGRADE_TASK_SEQUENCE_MASTER =
      ImmutableList.of(
          TaskType.SetNodeState,
          TaskType.CheckNodesAreSafeToTakeDown,
          TaskType.AnsibleClusterServerCtl,
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.WaitForEncryptionKeyInMemory,
          TaskType.CheckFollowerLag,
          TaskType.SetNodeState,
          TaskType.WaitStartingFromTime);

  private static final List<TaskType> ROLLING_UPGRADE_TASK_SEQUENCE_TSERVER =
      ImmutableList.of(
          TaskType.SetNodeState,
          TaskType.CheckUnderReplicatedTablets,
          TaskType.CheckNodesAreSafeToTakeDown,
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
          TaskType.SetNodeState,
          TaskType.WaitStartingFromTime);

  private static final List<TaskType> ROLLING_UPGRADE_TASK_SEQUENCE_TSERVER_ONLY =
      ImmutableList.of(
          TaskType.SetNodeState,
          TaskType.CheckUnderReplicatedTablets,
          TaskType.CheckNodesAreSafeToTakeDown,
          TaskType.ModifyBlackList,
          TaskType.WaitForLeaderBlacklistCompletion,
          TaskType.AnsibleClusterServerCtl,
          TaskType.AnsibleClusterServerCtl,
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.WaitForEncryptionKeyInMemory,
          TaskType.ModifyBlackList,
          TaskType.CheckFollowerLag,
          TaskType.SetNodeState,
          TaskType.WaitStartingFromTime);

  private static final List<TaskType> ROLLING_UPGRADE_TASK_SEQUENCE_INACTIVE_ROLE =
      ImmutableList.of(
          TaskType.AnsibleClusterServerCtl,
          TaskType.AnsibleClusterServerCtl, /* Stop master on non-master nodes */
          TaskType.AnsibleConfigureServers);

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

  @Before
  public void setup() throws Exception {
    rollbackUpgrade.setTaskUUID(UUID.randomUUID());
    setCheckNodesAreSafeToTakeDown(mockClient);
    setUnderReplicatedTabletsMock();
    setFollowerLagMock();
    lenient().when(mockYBClient.getClientWithConfig(any())).thenReturn(mockClient);

    factory
        .forUniverse(defaultUniverse)
        .setValue(UniverseConfKeys.autoFlagUpdateSleepTimeInMilliSeconds.getKey(), "0ms");

    UniverseDefinitionTaskParams.PrevYBSoftwareConfig ybSoftwareConfig =
        new UniverseDefinitionTaskParams.PrevYBSoftwareConfig();
    ybSoftwareConfig.setAutoFlagConfigVersion(1);
    ybSoftwareConfig.setSoftwareVersion("2.21.0.0-b1");
    TestHelper.updateUniversePrevSoftwareConfig(defaultUniverse, ybSoftwareConfig);
    TestHelper.updateUniverseIsRollbackAllowed(defaultUniverse, true);
    TestHelper.updateUniverseVersion(defaultUniverse, "2.21.0.0-b2");
  }

  private void updatePrevYbSoftwareConfig(String initialVersion, String targetVersion) {
    UniverseDefinitionTaskParams details = defaultUniverse.getUniverseDetails();
    PrevYBSoftwareConfig prevYBSoftwareConfig = new PrevYBSoftwareConfig();
    prevYBSoftwareConfig.setSoftwareVersion(initialVersion);
    prevYBSoftwareConfig.setTargetUpgradeSoftwareVersion(targetVersion);
    prevYBSoftwareConfig.setCanRollbackCatalogUpgrade(true);
    prevYBSoftwareConfig.setAllTserversUpgradedToYsqlMajorVersion(true);
    details.prevYBSoftwareConfig = prevYBSoftwareConfig;
    details.isSoftwareRollbackAllowed = true;
    defaultUniverse.setUniverseDetails(details);
    defaultUniverse.save();
  }

  private TaskInfo submitTask(RollbackUpgradeParams requestParams) {
    return submitTask(requestParams, TaskType.RollbackUpgrade, commissioner);
  }

  private TaskInfo submitTask(RollbackUpgradeParams requestParams, int expectedVersion) {
    return submitTask(requestParams, TaskType.RollbackUpgrade, commissioner, expectedVersion);
  }

  private int assertSequence(
      Map<Integer, List<TaskInfo>> subTasksByPosition,
      ServerType serverType,
      int startPosition,
      boolean isRollingUpgrade,
      boolean activeRole) {
    int position = startPosition;
    if (isRollingUpgrade) {
      List<TaskType> taskSequence =
          serverType == MASTER
              ? (activeRole
                  ? ROLLING_UPGRADE_TASK_SEQUENCE_MASTER
                  : ROLLING_UPGRADE_TASK_SEQUENCE_INACTIVE_ROLE)
              : ROLLING_UPGRADE_TASK_SEQUENCE_TSERVER;
      List<Integer> nodeOrder = getRollingUpgradeNodeOrder(serverType, activeRole);
      List<List<Integer>> nodesOrder =
          activeRole
              ? nodeOrder.stream()
                  .map(n -> Collections.singletonList(n))
                  .collect(Collectors.toList())
              : Collections.singletonList(nodeOrder);

      for (List<Integer> nodeIndexes : nodesOrder) {
        List<String> nodeNames =
            nodeIndexes.stream()
                .map(nodeIdx -> String.format("host-n%d", nodeIdx))
                .collect(Collectors.toList());
        int pos = position;
        // A bit hacky, but for TSERVER we need to do the tserver only (does not also have a master
        // process) for the tserver only nodes. Based on the ordering, our masters are node indexes
        // 3, 1, 2 - with nodes 4 and 5 being tserver only.
        if (nodeNames.size() == 1 && serverType == TSERVER) {
          if (nodeIndexes.get(0) >= 4) {
            taskSequence = ROLLING_UPGRADE_TASK_SEQUENCE_TSERVER_ONLY;
          } else {
            taskSequence = ROLLING_UPGRADE_TASK_SEQUENCE_TSERVER;
          }
        }
        for (TaskType type : taskSequence) {
          log.debug("exp {} {} - {}", nodeNames, pos++, type);
        }
        pos = position;
        for (TaskType type : taskSequence) {
          List<TaskInfo> tasks = subTasksByPosition.get(pos);
          TaskInfo task = tasks.get(0);
          String curNodeName = "???";
          if (task.getTaskParams().get("nodeName") != null) {
            curNodeName = task.getTaskParams().get("nodeName").asText();
          }
          TaskType taskType = task.getTaskType();
          log.debug("act {} {} - {}", curNodeName, pos++, taskType);
        }
        for (TaskType type : taskSequence) {
          List<TaskInfo> tasks = subTasksByPosition.get(position);
          TaskType taskType = tasks.get(0).getTaskType();
          UserTaskDetails.SubTaskGroupType subTaskGroupType = tasks.get(0).getSubTaskGroupType();
          // Leader blacklisting adds a ModifyBlackList task at position 0
          int numTasksToAssert = position == 0 ? 2 : nodeNames.size();
          assertEquals(numTasksToAssert, tasks.size());
          assertEquals(type, taskType);
          if (!NON_NODE_TASKS.contains(taskType)) {
            Map<String, Object> assertValues =
                nodeIndexes.size() == 1
                    ? new HashMap<>(ImmutableMap.of("nodeName", nodeNames.get(0), "nodeCount", 1))
                    : new HashMap<>(
                        ImmutableMap.of("nodeNames", nodeNames, "nodeCount", nodeNames.size()));

            if (taskType.equals(TaskType.AnsibleConfigureServers)) {
              String version = "2.21.0.0-b1";
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
            String version = "2.21.0.0-b1";
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

  protected List<Integer> getRollingUpgradeNodeOrder(ServerType serverType, boolean activeRole) {
    return serverType == MASTER
        ?
        // We need to check that the master leader is upgraded last.
        (activeRole ? Arrays.asList(3, 1, 2) : Arrays.asList(4, 5))
        :
        // We need to check that isAffinitized zone node is upgraded getFirst().
        defaultUniverse.getUniverseDetails().getReadOnlyClusters().isEmpty()
            ? Arrays.asList(3, 1, 2, 4, 5)
            :
            // Primary cluster getFirst(), then read replica.
            Arrays.asList(3, 1, 2, 4, 5, 8, 6, 7);
  }

  private int assertCommonTasks(
      Map<Integer, List<TaskInfo>> subTasksByPosition,
      int startPosition,
      UpgradeType type,
      boolean isFinalStep) {
    int position = startPosition;
    List<TaskType> commonNodeTasks = new ArrayList<>();

    if (type.name().equals("ROLLING_UPGRADE_TSERVER_ONLY") && !isFinalStep) {
      commonNodeTasks.add(TaskType.ModifyBlackList);
    }
    if (isFinalStep) {
      commonNodeTasks.addAll(
          ImmutableList.of(
              TaskType.EnablePitrConfig,
              TaskType.CheckSoftwareVersion,
              TaskType.UpdateSoftwareVersion,
              TaskType.UpdateUniverseState,
              TaskType.UniverseUpdateSucceeded));
    }
    for (TaskType commonNodeTask : commonNodeTasks) {
      assertTaskType(subTasksByPosition.get(position), commonNodeTask);
      position++;
    }
    return position;
  }

  @Test
  public void testRollbackRetries() {
    updatePrevYbSoftwareConfig("2.21.0.0-b1", "2.21.0.0-b2");
    RuntimeConfigEntry.upsert(
        defaultUniverse, UniverseConfKeys.autoFlagUpdateSleepTimeInMilliSeconds.getKey(), "0ms");
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
        TaskType.RollbackUpgrade,
        taskParams,
        false);
    checkUniverseNodesStates(taskParams.getUniverseUUID());
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertFalse(defaultUniverse.getUniverseDetails().isSoftwareRollbackAllowed);
    assertEquals(
        SoftwareUpgradeState.Ready, defaultUniverse.getUniverseDetails().softwareUpgradeState);
  }

  @Test
  public void testRollbackUpgradeInRollingManner() {
    updateDefaultUniverseTo5Nodes(true);
    updatePrevYbSoftwareConfig("2.21.0.0-b1", "2.21.0.0-b2");
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    RollbackUpgradeParams taskParams = new RollbackUpgradeParams();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.upgradeOption = UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE;

    mockDBServerVersion(
        "2.21.0.0-b2",
        "2.21.0.0-b1",
        defaultUniverse.getMasters().size() + defaultUniverse.getTServers().size());
    TaskInfo taskInfo = submitTask(taskParams, defaultUniverse.getVersion());
    verify(mockNodeManager, times(37)).nodeCommand(any(), any());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    int position = 0;
    assertTaskType(subTasksByPosition.get(position++), TaskType.CheckServiceLiveness);
    assertTaskType(subTasksByPosition.get(position++), TaskType.CheckNodeCommandExecution);
    assertTaskType(subTasksByPosition.get(position++), TaskType.CheckNodesAreSafeToTakeDown);
    assertTaskType(subTasksByPosition.get(position++), TaskType.UpdateConsistencyCheck);
    assertTaskType(subTasksByPosition.get(position++), TaskType.FreezeUniverse);
    assertTaskType(subTasksByPosition.get(position++), TaskType.UpdateUniverseState);
    assertTaskType(subTasksByPosition.get(position++), TaskType.RollbackAutoFlags);

    List<TaskInfo> downloadTasks = subTasksByPosition.get(position++);
    assertTaskType(downloadTasks, TaskType.AnsibleConfigureServers);
    assertEquals(5, downloadTasks.size());
    assertTaskType(subTasksByPosition.get(position++), TaskType.ModifyBlackList);
    position = assertSequence(subTasksByPosition, TSERVER, position, true, true);
    position = assertSequence(subTasksByPosition, MASTER, position, true, false);
    position = assertSequence(subTasksByPosition, MASTER, position, true, true);
    assertCommonTasks(subTasksByPosition, position, UpgradeType.ROLLING_UPGRADE, true);
    assertEquals(122, position);
    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertFalse(defaultUniverse.getUniverseDetails().isSoftwareRollbackAllowed);
    assertEquals(
        SoftwareUpgradeState.Ready, defaultUniverse.getUniverseDetails().softwareUpgradeState);
    assertNull(defaultUniverse.getUniverseDetails().prevYBSoftwareConfig);
  }

  @Test
  public void testRollbackUpgradeInNonRollingManner() {
    updateDefaultUniverseTo5Nodes(true);
    updatePrevYbSoftwareConfig("2.21.0.0-b1", "2.21.0.0-b2");
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    RollbackUpgradeParams taskParams = new RollbackUpgradeParams();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.upgradeOption = UpgradeTaskParams.UpgradeOption.NON_ROLLING_UPGRADE;

    mockDBServerVersion(
        "2.21.0.0-b2",
        "2.21.0.0-b1",
        defaultUniverse.getMasters().size() + defaultUniverse.getTServers().size());
    TaskInfo taskInfo = submitTask(taskParams, defaultUniverse.getVersion());
    verify(mockNodeManager, times(33)).nodeCommand(any(), any());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    int position = 0;
    assertTaskType(subTasksByPosition.get(position++), TaskType.UpdateConsistencyCheck);
    assertTaskType(subTasksByPosition.get(position++), TaskType.FreezeUniverse);
    assertTaskType(subTasksByPosition.get(position++), TaskType.UpdateUniverseState);
    assertTaskType(subTasksByPosition.get(position++), TaskType.RollbackAutoFlags);

    List<TaskInfo> downloadTasks = subTasksByPosition.get(position++);
    assertTaskType(downloadTasks, TaskType.AnsibleConfigureServers);
    assertEquals(5, downloadTasks.size());
    position = assertSequence(subTasksByPosition, TSERVER, position, false, true);
    position = assertSequence(subTasksByPosition, MASTER, position, false, false);
    position = assertSequence(subTasksByPosition, MASTER, position, false, true);
    assertCommonTasks(subTasksByPosition, position, UpgradeType.FULL_UPGRADE, true);
    assertEquals(21, position);
    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertFalse(defaultUniverse.getUniverseDetails().isSoftwareRollbackAllowed);
    assertEquals(
        SoftwareUpgradeState.Ready, defaultUniverse.getUniverseDetails().softwareUpgradeState);
    assertNull(defaultUniverse.getUniverseDetails().prevYBSoftwareConfig);
  }

  @Test
  public void testRollbackPartialUpgrade() {
    updateDefaultUniverseTo5Nodes(true);
    updatePrevYbSoftwareConfig("2.21.0.0-b1", "2.21.0.0-b2");
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    RollbackUpgradeParams taskParams = new RollbackUpgradeParams();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.upgradeOption = UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE;

    int masterTserverNodesCount =
        defaultUniverse.getMasters().size() + defaultUniverse.getTServers().size();
    mockDBServerVersion(
        "2.21.0.0-b2", masterTserverNodesCount - 1, "2.21.0.0-b1", masterTserverNodesCount + 1);
    TaskInfo taskInfo = submitTask(taskParams, defaultUniverse.getVersion());
    // 4 download + 4x3 (stop/config/start tserver) + 3x3 (stop/config/start master)
    // + 1x2 (stop inactive master + config)
    verify(mockNodeManager, times(29)).nodeCommand(any(), any());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    int position = 0;
    assertTaskType(subTasksByPosition.get(position++), TaskType.CheckServiceLiveness);
    assertTaskType(subTasksByPosition.get(position++), TaskType.CheckNodeCommandExecution);
    assertTaskType(subTasksByPosition.get(position++), TaskType.CheckNodesAreSafeToTakeDown);
    assertTaskType(subTasksByPosition.get(position++), TaskType.UpdateConsistencyCheck);
    assertTaskType(subTasksByPosition.get(position++), TaskType.FreezeUniverse);
    assertTaskType(subTasksByPosition.get(position++), TaskType.UpdateUniverseState);
    assertTaskType(subTasksByPosition.get(position++), TaskType.RollbackAutoFlags);
    List<TaskInfo> downloadTasks = subTasksByPosition.get(position++);
    assertTaskType(downloadTasks, TaskType.AnsibleConfigureServers);
    assertEquals(4, downloadTasks.size());
    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertFalse(defaultUniverse.getUniverseDetails().isSoftwareRollbackAllowed);
    assertEquals(
        SoftwareUpgradeState.Ready, defaultUniverse.getUniverseDetails().softwareUpgradeState);
    assertNull(defaultUniverse.getUniverseDetails().prevYBSoftwareConfig);
  }

  @Test
  public void testRollbackYsqlMajorVersionSoftwareUpgrade() throws Exception {

    String baseVersion = "2024.2.2.0-b1";
    String targetVersion = "2025.1.0.0-b1";

    InstanceType.upsert(
        defaultProvider.getUuid(),
        "m3.medium",
        0,
        Double.valueOf(0),
        InstanceTypeDetails.createGCPDefault());
    updatePrevYbSoftwareConfig(baseVersion, targetVersion);
    when(mockNodeUniverseManager.runCommand(any(), any(), anyList()))
        .thenReturn(ShellResponse.create(0, StringUtils.EMPTY));
    when(mockSoftwareUpgradeHelper.isYsqlMajorVersionUpgradeRequired(
            any(), anyString(), anyString()))
        .thenReturn(true);
    when(mockSoftwareUpgradeHelper.getYsqlMajorCatalogUpgradeState(any()))
        .thenReturn(YsqlMajorCatalogUpgradeState.YSQL_MAJOR_CATALOG_UPGRADE_PENDING_ROLLBACK);
    when(mockClient.setFlag(any(), anyString(), anyString(), anyBoolean())).thenReturn(true);
    when(mockClient.getYsqlMajorCatalogUpgradeState())
        .thenReturn(
            new GetYsqlMajorCatalogUpgradeStateResponse(
                0L, null, null, YsqlMajorCatalogUpgradeState.YSQL_MAJOR_CATALOG_UPGRADE_PENDING));
    mockDBServerVersion(
        targetVersion,
        baseVersion,
        defaultUniverse.getMasters().size() + defaultUniverse.getTServers().size());

    RollbackUpgradeParams taskParams = new RollbackUpgradeParams();
    taskParams.upgradeOption = UpgradeOption.NON_ROLLING_UPGRADE;
    taskParams.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());

    TaskInfo taskInfo = submitTask(taskParams, defaultUniverse.getVersion());

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(getPrecheckTasks(false))
        .addTasks(TaskType.UpdateUniverseState)
        .addTasks(TaskType.RollbackAutoFlags)
        .addSimultaneousTasks(
            TaskType.AnsibleConfigureServers, defaultUniverse.getTServers().size())
        .addSimultaneousTasks(TaskType.SetFlagInMemory, defaultUniverse.getMasters().size())
        .addSimultaneousTasks(TaskType.SetFlagInMemory, defaultUniverse.getTServers().size())
        .addSimultaneousTasks(TaskType.AnsibleConfigureServers, defaultUniverse.getMasters().size())
        .addSimultaneousTasks(
            TaskType.AnsibleConfigureServers, defaultUniverse.getTServers().size())
        .upgradeRound(UpgradeOption.NON_ROLLING_UPGRADE)
        .withContext(
            UpgradeTaskBase.UpgradeContext.builder()
                .reconfigureMaster(false)
                .runBeforeStopping(false)
                .processInactiveMaster(false)
                .processTServersFirst(true)
                .targetSoftwareVersion(baseVersion)
                .build())
        .task(TaskType.AnsibleConfigureServers)
        .applyToTservers()
        .addTasks(TaskType.RollbackYsqlMajorVersionCatalogUpgrade)
        .addTasks(TaskType.UpdateSoftwareUpdatePrevConfig)
        .upgradeRound(UpgradeOption.NON_ROLLING_UPGRADE)
        .withContext(
            UpgradeTaskBase.UpgradeContext.builder()
                .reconfigureMaster(false)
                .runBeforeStopping(false)
                .processInactiveMaster(false)
                .processTServersFirst(false)
                .targetSoftwareVersion(baseVersion)
                .build())
        .task(TaskType.AnsibleConfigureServers)
        .applyToMasters()
        .addSimultaneousTasks(TaskType.SetFlagInMemory, defaultUniverse.getMasters().size())
        .addSimultaneousTasks(TaskType.SetFlagInMemory, defaultUniverse.getTServers().size())
        .addSimultaneousTasks(TaskType.AnsibleConfigureServers, defaultUniverse.getMasters().size())
        .addSimultaneousTasks(
            TaskType.AnsibleConfigureServers, defaultUniverse.getTServers().size())
        .addTasks(TaskType.EnablePitrConfig)
        .addSimultaneousTasks(TaskType.CheckSoftwareVersion, defaultUniverse.getTServers().size())
        .addTasks(TaskType.UpdateSoftwareVersion)
        .addTasks(TaskType.UpdateUniverseState)
        .verifyTasks(taskInfo.getSubTasks());

    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertFalse(defaultUniverse.getUniverseDetails().isSoftwareRollbackAllowed);
    assertNull(defaultUniverse.getUniverseDetails().prevYBSoftwareConfig);
    assertEquals(
        baseVersion,
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion);
  }

  private MockUpgrade initMockUpgrade() {
    return initMockUpgrade(RollbackUpgrade.class);
  }
}
