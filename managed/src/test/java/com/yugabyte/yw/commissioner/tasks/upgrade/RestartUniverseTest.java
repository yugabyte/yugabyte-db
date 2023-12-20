// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType.MASTER;
import static com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType.TSERVER;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.HashMap;
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
public class RestartUniverseTest extends UpgradeTaskTest {

  @InjectMocks private RestartUniverse restartUniverse;

  private static final List<TaskType> ROLLING_RESTART_TASK_SEQUENCE_MASTER =
      ImmutableList.of(
          TaskType.SetNodeState,
          TaskType.AnsibleClusterServerCtl,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.WaitForEncryptionKeyInMemory,
          TaskType.WaitForFollowerLag,
          TaskType.SetNodeState);

  private static final List<TaskType> ROLLING_RESTART_TASK_SEQUENCE_TSERVER =
      ImmutableList.of(
          TaskType.SetNodeState,
          TaskType.ModifyBlackList,
          TaskType.WaitForLeaderBlacklistCompletion,
          TaskType.AnsibleClusterServerCtl,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.WaitForEncryptionKeyInMemory,
          TaskType.ModifyBlackList,
          TaskType.WaitForFollowerLag,
          TaskType.SetNodeState);

  @Override
  @Before
  public void setUp() {
    super.setUp();

    restartUniverse.setUserTaskUUID(UUID.randomUUID());
  }

  private TaskInfo submitTask(UpgradeTaskParams requestParams) {
    return submitTask(requestParams, TaskType.RestartUniverse, commissioner);
  }

  private int assertSequence(
      Map<Integer, List<TaskInfo>> subTasksByPosition, ServerType serverType, int startPosition) {
    int position = startPosition;
    List<TaskType> taskSequence =
        serverType == MASTER
            ? ROLLING_RESTART_TASK_SEQUENCE_MASTER
            : ROLLING_RESTART_TASK_SEQUENCE_TSERVER;
    List<Integer> nodeOrder = getRollingUpgradeNodeOrder(serverType);
    for (int nodeIdx : nodeOrder) {
      String nodeName = String.format("host-n%d", nodeIdx);
      for (TaskType type : taskSequence) {
        List<TaskInfo> tasks = subTasksByPosition.get(position);
        TaskType taskType = tasks.get(0).getTaskType();

        assertEquals(1, tasks.size());
        assertEquals(type, taskType);
        if (!NON_NODE_TASKS.contains(taskType)) {
          Map<String, Object> assertValues =
              new HashMap<>(ImmutableMap.of("nodeName", nodeName, "nodeCount", 1));
          assertNodeSubTask(tasks, assertValues);
        }
        position++;
      }
    }
    return position;
  }

  @Test
  public void testRollingRestart() {
    UpgradeTaskParams taskParams = new UpgradeTaskParams();
    TaskInfo taskInfo = submitTask(taskParams);
    verify(mockNodeManager, times(12)).nodeCommand(any(), any());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    int position = 0;
    assertTaskType(subTasksByPosition.get(position++), TaskType.FreezeUniverse);
    position = assertSequence(subTasksByPosition, MASTER, position);
    assertTaskType(subTasksByPosition.get(position++), TaskType.ModifyBlackList);
    position = assertSequence(subTasksByPosition, TSERVER, position);
    assertEquals(59, position);
    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());
  }
}
