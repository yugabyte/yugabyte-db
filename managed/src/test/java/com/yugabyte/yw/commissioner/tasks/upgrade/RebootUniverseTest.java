// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeOption;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class RebootUniverseTest extends UpgradeTaskTest {

  @InjectMocks private RebootUniverse rebootUniverse;

  private static final List<TaskType> ROLLING_REBOOT_TASK_SEQUENCE =
      ImmutableList.of(
          TaskType.SetNodeState,
          TaskType.CheckUnderReplicatedTablets,
          TaskType.RunHooks,
          TaskType.ModifyBlackList,
          TaskType.WaitForLeaderBlacklistCompletion,
          TaskType.AnsibleClusterServerCtl, // master
          TaskType.AnsibleClusterServerCtl, // tserver
          TaskType.RebootServer,
          TaskType.AnsibleClusterServerCtl, // master
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.AnsibleClusterServerCtl, // tserver
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.WaitForEncryptionKeyInMemory,
          TaskType.ModifyBlackList,
          TaskType.CheckFollowerLag, // master
          TaskType.CheckFollowerLag, // tserver
          TaskType.RunHooks,
          TaskType.SetNodeState);

  @Override
  @Before
  public void setUp() {
    super.setUp();
    attachHooks("RebootUniverse");
    rebootUniverse.setUserTaskUUID(UUID.randomUUID());

    setUnderReplicatedTabletsMock();
    setFollowerLagMock();
  }

  private TaskInfo submitTask(UpgradeTaskParams requestParams) {
    return submitTask(requestParams, TaskType.RebootUniverse, commissioner);
  }

  private int assertSequence(Map<Integer, List<TaskInfo>> subTasksByPosition, int startPosition) {
    int position = startPosition;
    for (int i = 0; i < 3; i++) {
      for (TaskType type : ROLLING_REBOOT_TASK_SEQUENCE) {
        List<TaskInfo> tasks = subTasksByPosition.get(position++);
        TaskType taskType = tasks.get(0).getTaskType();
        assertEquals(1, tasks.size());
        assertEquals(type, taskType);
      }
    }
    return position;
  }

  @Test
  public void testNonRollingReboot() {
    UpgradeTaskParams taskParams = new UpgradeTaskParams();
    taskParams.upgradeOption = UpgradeOption.NON_ROLLING_UPGRADE;
    assertThrows(RuntimeException.class, () -> submitTask(taskParams));
    verify(mockNodeManager, times(0)).nodeCommand(any(), any());
  }

  @Test
  public void tesNonRestartUpgradeReboot() {
    UpgradeTaskParams taskParams = new UpgradeTaskParams();
    taskParams.upgradeOption = UpgradeOption.NON_RESTART_UPGRADE;
    assertThrows(RuntimeException.class, () -> submitTask(taskParams));
    verify(mockNodeManager, times(0)).nodeCommand(any(), any());
  }

  @Test
  public void testRollingReboot() {
    UpgradeTaskParams taskParams = new UpgradeTaskParams();
    TaskInfo taskInfo = submitTask(taskParams);
    verify(mockNodeManager, times(27)).nodeCommand(any(), any());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    int position = 0;
    assertTaskType(subTasksByPosition.get(position++), TaskType.FreezeUniverse);
    assertTaskType(subTasksByPosition.get(position++), TaskType.RunHooks);
    assertTaskType(subTasksByPosition.get(position++), TaskType.ModifyBlackList);
    position = assertSequence(subTasksByPosition, position);
    assertTaskType(subTasksByPosition.get(position++), TaskType.RunHooks);
    assertTaskType(subTasksByPosition.get(position++), TaskType.UniverseUpdateSucceeded);
    assertTaskType(subTasksByPosition.get(position++), TaskType.ModifyBlackList);
    assertEquals(66, position);
    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());
  }
}
