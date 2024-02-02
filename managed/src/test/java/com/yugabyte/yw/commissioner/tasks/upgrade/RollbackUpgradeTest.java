// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType.DownloadingSoftware;
import static com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType.MASTER;
import static com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType.TSERVER;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.TestHelper;
import com.yugabyte.yw.forms.RollbackUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.SoftwareUpgradeState;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import junitparams.JUnitParamsRunner;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

@RunWith(JUnitParamsRunner.class)
public class RollbackUpgradeTest extends UpgradeTaskTest {

  @Rule public MockitoRule rule = MockitoJUnit.rule();

  @InjectMocks private RollbackUpgrade rollbackUpgrade;

  private static final List<TaskType> ROLLING_UPGRADE_TASK_SEQUENCE_MASTER =
      ImmutableList.of(
          TaskType.SetNodeState,
          TaskType.AnsibleClusterServerCtl,
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.WaitForEncryptionKeyInMemory,
          TaskType.CheckFollowerLag,
          TaskType.SetNodeState);

  private static final List<TaskType> ROLLING_UPGRADE_TASK_SEQUENCE_TSERVER =
      ImmutableList.of(
          TaskType.SetNodeState,
          TaskType.CheckUnderReplicatedTablets,
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
          TaskType.SetNodeState);

  private static final List<TaskType> ROLLING_UPGRADE_TASK_SEQUENCE_INACTIVE_ROLE =
      ImmutableList.of(
          TaskType.SetNodeState,
          TaskType.AnsibleClusterServerCtl,
          TaskType.AnsibleConfigureServers,
          TaskType.SetNodeState);

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
    setUnderReplicatedTabletsMock();
    setFollowerLagMock();
    setLeaderlessTabletsMock();

    updateDefaultUniverseTo5Nodes(true);

    UniverseDefinitionTaskParams.PrevYBSoftwareConfig ybSoftwareConfig =
        new UniverseDefinitionTaskParams.PrevYBSoftwareConfig();
    ybSoftwareConfig.setAutoFlagConfigVersion(1);
    ybSoftwareConfig.setSoftwareVersion("2.21.0.0-b1");
    TestHelper.updateUniversePrevSoftwareConfig(defaultUniverse, ybSoftwareConfig);
    TestHelper.updateUniverseIsRollbackAllowed(defaultUniverse, true);
    TestHelper.updateUniverseVersion(defaultUniverse, "2.21.0.0-b2");
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
        (activeRole ? Arrays.asList(1, 3, 2) : Arrays.asList(4, 5))
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
    RollbackUpgradeParams taskParams = new RollbackUpgradeParams();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = -1;
    super.verifyTaskRetries(
        defaultCustomer,
        CustomerTask.TaskType.RollbackUpgrade,
        CustomerTask.TargetType.Universe,
        defaultUniverse.getUniverseUUID(),
        TaskType.RollbackUpgrade,
        taskParams,
        false);
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertFalse(defaultUniverse.getUniverseDetails().isSoftwareRollbackAllowed);
    assertEquals(
        SoftwareUpgradeState.Ready, defaultUniverse.getUniverseDetails().softwareUpgradeState);
  }

  @Test
  public void testRollbackUpgradeInRollingManner() {
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    RollbackUpgradeParams taskParams = new RollbackUpgradeParams();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.upgradeOption = UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE;

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
    assertTaskType(subTasksByPosition.get(position++), TaskType.FreezeUniverse);
    assertTaskType(subTasksByPosition.get(position++), TaskType.UpdateUniverseState);
    assertTaskType(subTasksByPosition.get(position++), TaskType.RollbackAutoFlags);

    List<TaskInfo> downloadTasks = subTasksByPosition.get(position++);
    assertTaskType(downloadTasks, TaskType.AnsibleConfigureServers);
    assertEquals(5, downloadTasks.size());
    assertTaskType(subTasksByPosition.get(position++), TaskType.ModifyBlackList);
    position = assertSequence(subTasksByPosition, TSERVER, position, true, true);
    position = assertSequence(subTasksByPosition, MASTER, position, true, true);
    position = assertSequence(subTasksByPosition, MASTER, position, true, false);
    assertCommonTasks(subTasksByPosition, position, UpgradeType.ROLLING_UPGRADE, true);
    assertEquals(105, position);
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
    assertTaskType(subTasksByPosition.get(position++), TaskType.FreezeUniverse);
    assertTaskType(subTasksByPosition.get(position++), TaskType.UpdateUniverseState);
    assertTaskType(subTasksByPosition.get(position++), TaskType.RollbackAutoFlags);

    List<TaskInfo> downloadTasks = subTasksByPosition.get(position++);
    assertTaskType(downloadTasks, TaskType.AnsibleConfigureServers);
    assertEquals(5, downloadTasks.size());
    position = assertSequence(subTasksByPosition, TSERVER, position, false, true);
    position = assertSequence(subTasksByPosition, MASTER, position, false, true);
    position = assertSequence(subTasksByPosition, MASTER, position, false, false);
    assertCommonTasks(subTasksByPosition, position, UpgradeType.FULL_UPGRADE, true);
    assertEquals(20, position);
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
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    RollbackUpgradeParams taskParams = new RollbackUpgradeParams();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.upgradeOption = UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE;

    int masterTserverNodesCount =
        defaultUniverse.getMasters().size() + defaultUniverse.getTServers().size();
    mockDBServerVersion(
        "2.21.0.0-b2", masterTserverNodesCount - 1, "2.21.0.0-b1", masterTserverNodesCount + 1);
    TaskInfo taskInfo = submitTask(taskParams, defaultUniverse.getVersion());
    verify(mockNodeManager, times(29)).nodeCommand(any(), any());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    int position = 0;
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
}
