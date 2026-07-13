// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.commissioner.MockUpgrade;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.RestartTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.List;
import java.util.Set;
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

  @Override
  @Before
  public void setUp() {
    super.setUp();
    setCheckNodesAreSafeToTakeDown(mockClient);
    restartUniverse.setUserTaskUUID(UUID.randomUUID());
    attachHooks("RestartUniverse");

    setUnderReplicatedTabletsMock();
    setFollowerLagMock();
  }

  private TaskInfo submitTask(RestartTaskParams requestParams) {
    return submitTask(requestParams, TaskType.RestartUniverse, commissioner);
  }

  @Test
  public void testRollingRestart() {
    RestartTaskParams taskParams = new RestartTaskParams();
    TaskInfo taskInfo = submitTask(taskParams);

    initMockUpgrade()
        .precheckTasks(getPrecheckTasks(true))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE)
        .applyRound()
        .verifyTasks(taskInfo.getSubTasks());
    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());
  }

  @Test
  public void testRollingRestartNoWaitAfterLeaderBlacklistByDefault() {
    // The optional wait-after-leader-blacklist conf defaults to 0, so no WaitForDuration subtask
    // should be inserted between blacklisting leaders and stopping the tserver.
    RestartTaskParams taskParams = new RestartTaskParams();
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());

    List<TaskInfo> waitForDurations =
        taskInfo.getSubTasks().stream()
            .filter(t -> t.getTaskType() == TaskType.WaitForDuration)
            .collect(Collectors.toList());
    assertEquals(0, waitForDurations.size());
  }

  @Test
  public void testRollingRestartWaitsAfterLeaderBlacklistWhenConfigured() {
    // Make the wait-after-leader-blacklist runtime config non-zero.
    factory
        .forUniverse(defaultUniverse)
        .setValue(UniverseConfKeys.ybUpgradeBlacklistLeaderWaitAfterCompletion.getKey(), "30000ms");

    RestartTaskParams taskParams = new RestartTaskParams();
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    List<TaskInfo> waitForDurations =
        subTasks.stream()
            .filter(t -> t.getTaskType() == TaskType.WaitForDuration)
            .collect(Collectors.toList());
    List<TaskInfo> leaderBlacklistWaits =
        subTasks.stream()
            .filter(t -> t.getTaskType() == TaskType.WaitForLeaderBlacklistCompletion)
            .collect(Collectors.toList());

    // One WaitForDuration is inserted per leader-blacklist completion (one per restarted tserver).
    assertTrue("Expected at least one WaitForDuration subtask", waitForDurations.size() > 0);
    assertEquals(leaderBlacklistWaits.size(), waitForDurations.size());

    // Every WaitForDuration must come immediately after a WaitForLeaderBlacklistCompletion and
    // before any subsequent stop, i.e. its position is exactly one past a blacklist-wait position.
    Set<Integer> blacklistWaitPositions =
        leaderBlacklistWaits.stream().map(TaskInfo::getPosition).collect(Collectors.toSet());
    for (TaskInfo waitForDuration : waitForDurations) {
      assertTrue(
          "WaitForDuration at position "
              + waitForDuration.getPosition()
              + " is not immediately after a WaitForLeaderBlacklistCompletion",
          blacklistWaitPositions.contains(waitForDuration.getPosition() - 1));
      assertTrue(
          "WaitForDuration should carry a wait time",
          waitForDuration.getTaskParams().hasNonNull("waitTime"));
    }
  }

  @Test
  public void testRollingRestartRetries() {
    RestartTaskParams taskParams = new RestartTaskParams();
    taskParams.expectedUniverseVersion = -1;
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.creatingUser = defaultUser;
    TestUtils.setFakeHttpContext(defaultUser);
    super.verifyTaskRetries(
        defaultCustomer,
        CustomerTask.TaskType.RestartUniverse,
        CustomerTask.TargetType.Universe,
        defaultUniverse.getUniverseUUID(),
        TaskType.RestartUniverse,
        taskParams,
        false);
    checkUniverseNodesStates(taskParams.getUniverseUUID());
  }

  private MockUpgrade initMockUpgrade() {
    return initMockUpgrade(RestartUniverse.class);
  }
}
