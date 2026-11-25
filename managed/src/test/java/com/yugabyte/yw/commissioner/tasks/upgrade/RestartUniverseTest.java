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
import com.yugabyte.yw.models.Universe;
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
import play.libs.Json;

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
    // Comprehensive prechecks require node command execution which is not available in unit tests.
    factory
        .forUniverse(defaultUniverse)
        .setValue(UniverseConfKeys.enableComprehensivePrechecks.getKey(), "false");
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
    // Comprehensive prechecks require node command execution which is not available in unit tests.
    factory
        .forUniverse(defaultUniverse)
        .setValue(UniverseConfKeys.enableComprehensivePrechecks.getKey(), "false");
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

  // A restart must fail before it stops any process when KMS cannot return the universe key.
  @Test
  public void testRollingRestartFailsBeforeStopWhenKmsKeyUnavailable() {
    verifyRestartFailsBeforeStop(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE);
  }

  @Test
  public void testNonRollingRestartFailsBeforeStopWhenKmsKeyUnavailable() {
    verifyRestartFailsBeforeStop(UpgradeTaskParams.UpgradeOption.NON_ROLLING_UPGRADE);
  }

  // The check must still ask KMS when the universe has keys but none is marked active.
  @Test
  public void testRestartFailsKmsCheckWhenNoKeyIsActive() {
    setUpUniverseKeyInKms(
        defaultUniverse.getUniverseUUID(), false /* kmsWorks */, false /* activateKey */);
    verifyRestartFailsBeforeStop(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, false);
  }

  // A retry finishes a started restart, so with the key in the YBA cache it skips the check.
  @Test
  public void testRestartRetryWithCachedKeySkipsKmsCheck() {
    TaskInfo taskInfo = submitRestartRetryWithKmsDown(true /* keyCached */);
    assertEquals(taskInfo.getErrorMessage(), Success, taskInfo.getTaskState());
    assertTrue(
        taskInfo.getSubTasks().stream()
            .noneMatch(t -> t.getTaskType() == TaskType.CheckUniverseKeyInKms));
  }

  // A YBA restart empties the cache, so the retry checks the key before it stops a process.
  @Test
  public void testRestartRetryWithEmptyKeyCacheRunsKmsCheck() {
    TaskInfo taskInfo = submitRestartRetryWithKmsDown(false /* keyCached */);
    assertEquals(TaskInfo.State.Failure, taskInfo.getTaskState());
    assertKmsCheckFailed(taskInfo);
    assertTrue(
        taskInfo.getSubTasks().stream()
            .noneMatch(t -> t.getTaskType() == TaskType.AnsibleClusterServerCtl));
  }

  private TaskInfo submitRestartRetryWithKmsDown(boolean keyCached) {
    setUpUniverseKeyInKms(defaultUniverse.getUniverseUUID(), false /* kmsWorks */);
    if (keyCached) {
      cacheUniverseKey(defaultUniverse.getUniverseUUID());
    }
    UUID previousTaskUUID = UUID.randomUUID();
    TaskInfo previousTask = new TaskInfo(TaskType.RestartUniverse, previousTaskUUID);
    previousTask.setTaskParams(Json.newObject());
    previousTask.setOwner("");
    previousTask.setTaskState(TaskInfo.State.Failure);
    previousTask.save();
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            u -> u.getUniverseDetails().updatingTaskUUID = previousTaskUUID);
    RestartTaskParams taskParams = new RestartTaskParams();
    taskParams.upgradeOption = UpgradeTaskParams.UpgradeOption.NON_ROLLING_UPGRADE;
    taskParams.setPreviousTaskUUID(previousTaskUUID);
    // -1: saving updatingTaskUUID changed the universe version.
    return submitTask(taskParams, TaskType.RestartUniverse, commissioner, -1);
  }

  private void verifyRestartFailsBeforeStop(UpgradeTaskParams.UpgradeOption option) {
    verifyRestartFailsBeforeStop(option, true);
  }

  private void verifyRestartFailsBeforeStop(
      UpgradeTaskParams.UpgradeOption option, boolean setUpKey) {
    if (setUpKey) {
      setUpUniverseKeyInKms(defaultUniverse.getUniverseUUID(), false /* kmsWorks */);
    }
    RestartTaskParams taskParams = new RestartTaskParams();
    taskParams.upgradeOption = option;
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(TaskInfo.State.Failure, taskInfo.getTaskState());
    assertKmsCheckFailed(taskInfo);
    assertTrue(
        taskInfo.getSubTasks().stream()
            .noneMatch(t -> t.getTaskType() == TaskType.AnsibleClusterServerCtl));
  }
}
