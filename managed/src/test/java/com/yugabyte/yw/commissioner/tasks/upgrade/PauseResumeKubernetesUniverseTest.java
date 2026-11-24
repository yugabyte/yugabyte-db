// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.UUID;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;

// Silent: the tasks fail before most base-class stubs are used.
@RunWith(MockitoJUnitRunner.Silent.class)
public class PauseResumeKubernetesUniverseTest extends KubernetesUpgradeTaskTest {

  // Pause must fail in the KMS check before it stops any pod.
  @Test
  public void testPauseFailsKmsCheckBeforePodStop() {
    TaskInfo taskInfo = submitPauseWithKmsDown();
    assertEquals(Failure, taskInfo.getTaskState());
    assertKmsCheckFailed(taskInfo);
    assertNoPodCommand(taskInfo);
  }

  // A pause that fails in the KMS check never froze the universe, so YBA refuses a retry.
  @Test
  public void testPauseFailedInKmsCheckIsNotRetryable() {
    TaskInfo taskInfo = submitPauseWithKmsDown();
    assertKmsCheckFailed(taskInfo);
    CustomerTask customerTask =
        CustomerTask.create(
            defaultCustomer,
            defaultUniverse.getUniverseUUID(),
            taskInfo.getUuid(),
            CustomerTask.TargetType.Universe,
            CustomerTask.TaskType.Pause,
            defaultUniverse.getName());
    assertFalse(customerTaskManager.isTaskRetryable(customerTask, taskInfo));
  }

  // Resume must fail in the KMS check before it starts any pod.
  @Test
  public void testResumeFailsKmsCheckBeforePodStart() {
    setupUniverseSingleAZ(true /* setMasters */, false);
    TaskInfo taskInfo = submitResumeWithKmsDown(null /* previousTaskUUID */);
    assertEquals(Failure, taskInfo.getTaskState());
    assertKmsCheckFailed(taskInfo);
    assertNoPodCommand(taskInfo);
  }

  // A retry with the key in the YBA cache skips the check, so a KMS outage does not block it.
  @Test
  public void testResumeRetrySkipsKmsCheck() {
    TaskInfo taskInfo = submitResumeRetryWithKmsDown(true /* keyCached */);
    assertTrue(
        taskInfo.getSubTasks().stream()
            .noneMatch(t -> t.getTaskType() == TaskType.CheckUniverseKeyInKms));
    assertTrue(
        taskInfo.getSubTasks().stream()
            .anyMatch(t -> t.getTaskType() == TaskType.KubernetesCommandExecutor));
  }

  // A YBA restart empties the cache, so the retry checks the key before it starts pods.
  @Test
  public void testResumeRetryWithEmptyKeyCacheRunsKmsCheck() {
    TaskInfo taskInfo = submitResumeRetryWithKmsDown(false /* keyCached */);
    assertEquals(Failure, taskInfo.getTaskState());
    assertKmsCheckFailed(taskInfo);
    assertNoPodCommand(taskInfo);
  }

  private TaskInfo submitPauseWithKmsDown() {
    setupUniverseSingleAZ(true /* setMasters */, false);
    setUpUniverseKeyInKms(defaultUniverse.getUniverseUUID(), false /* kmsWorks */);
    PauseKubernetesUniverse.Params params = new PauseKubernetesUniverse.Params();
    params.customerUUID = defaultCustomer.getUuid();
    return submit(params, TaskType.PauseKubernetesUniverse);
  }

  private TaskInfo submitResumeRetryWithKmsDown(boolean keyCached) {
    UUID previousTaskUUID = UUID.randomUUID();
    TaskInfo previousTask = new TaskInfo(TaskType.ResumeKubernetesUniverse, previousTaskUUID);
    previousTask.setTaskParams(Json.newObject());
    previousTask.setOwner("");
    previousTask.setTaskState(Failure);
    previousTask.save();
    setupUniverseSingleAZ(true /* setMasters */, false);
    if (keyCached) {
      cacheUniverseKey(defaultUniverse.getUniverseUUID());
    }
    return submitResumeWithKmsDown(previousTaskUUID);
  }

  private TaskInfo submitResumeWithKmsDown(UUID previousTaskUUID) {
    setUpUniverseKeyInKms(defaultUniverse.getUniverseUUID(), false /* kmsWorks */);
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            u -> {
              u.getUniverseDetails().universePaused = true;
              u.getUniverseDetails().updatingTaskUUID = previousTaskUUID;
            });
    ResumeKubernetesUniverse.Params params = new ResumeKubernetesUniverse.Params();
    params.customerUUID = defaultCustomer.getUuid();
    params.setPreviousTaskUUID(previousTaskUUID);
    return submit(params, TaskType.ResumeKubernetesUniverse);
  }

  // Not submitTask: it pins the universe version, and these tests save the universe again.
  private TaskInfo submit(UpgradeTaskParams params, TaskType taskType) {
    params.setUniverseUUID(defaultUniverse.getUniverseUUID());
    params.clusters = defaultUniverse.getUniverseDetails().clusters;
    params.nodePrefix = NODE_PREFIX;
    params.expectedUniverseVersion = -1;
    try {
      return waitForTask(commissioner.submit(taskType, params));
    } catch (InterruptedException e) {
      throw new RuntimeException(e);
    }
  }

  private void assertNoPodCommand(TaskInfo taskInfo) {
    assertTrue(
        taskInfo.getSubTasks().stream()
            .noneMatch(t -> t.getTaskType() == TaskType.KubernetesCommandExecutor));
  }
}
