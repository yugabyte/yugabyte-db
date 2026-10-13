// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.rollback;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.commissioner.tasks.subtasks.UpdateUniverseSoftwareUpgradeState;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.SoftwareUpgradeState;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class SoftwareUpgradeRollbackComputerTest extends FakeDBApplication {

  private SoftwareUpgradeRollbackComputer computer;
  private Universe universe;
  private TaskInfo failedUpgrade;

  @Before
  public void setUp() {
    computer = app.injector().instanceOf(SoftwareUpgradeRollbackComputer.class);
    universe = ModelFactory.createUniverse(ModelFactory.testCustomer().getId());
    failedUpgrade = new TaskInfo(TaskType.SoftwareUpgradeYB, null);
    failedUpgrade.setOwner("test");
    failedUpgrade.setTaskState(TaskInfo.State.Failure);
    failedUpgrade.setTaskParams(Json.newObject());
    failedUpgrade.save();
  }

  /** Records an UpdateUniverseState subtask of the failed upgrade. */
  private void addStateSubTask(
      SoftwareUpgradeState state, boolean rollbackAllowed, TaskInfo.State taskState) {
    UpdateUniverseSoftwareUpgradeState.Params params =
        new UpdateUniverseSoftwareUpgradeState.Params();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.state = state;
    params.isSoftwareRollbackAllowed = rollbackAllowed;
    TaskInfo subTask = new TaskInfo(TaskType.UpdateUniverseState, null);
    subTask.setOwner("test");
    subTask.setParentUuid(failedUpgrade.getUuid());
    subTask.setPosition(failedUpgrade.getSubTasks().size());
    subTask.setTaskState(taskState);
    subTask.setTaskParams(Json.toJson(params));
    subTask.save();
  }

  private boolean canRollbackWith(UUID owner, SoftwareUpgradeState state, boolean rollbackAllowed) {
    Universe.saveDetails(
        universe.getUniverseUUID(),
        u -> {
          UniverseDefinitionTaskParams details = u.getUniverseDetails();
          details.updatingTaskUUID = owner;
          details.placementModificationTaskUuid = owner;
          details.softwareUpgradeState = state;
          details.isSoftwareRollbackAllowed = rollbackAllowed;
          u.setUniverseDetails(details);
        });
    return computer.canRollback(failedUpgrade, universe.getUniverseUUID());
  }

  @Test
  public void testAllowedAfterUpgradeStarted() {
    assertTrue(canRollbackWith(failedUpgrade.getUuid(), SoftwareUpgradeState.UpgradeFailed, true));
  }

  @Test
  public void testRejectedBeforeUpgradeStarted() {
    // The Upgrading step never ran, so the state and flag still describe the previous upgrade.
    assertFalse(canRollbackWith(failedUpgrade.getUuid(), SoftwareUpgradeState.Ready, false));
    assertFalse(canRollbackWith(failedUpgrade.getUuid(), SoftwareUpgradeState.Ready, true));
  }

  @Test
  public void testAllowedWhenFailedAfterEndOfUpgradeState() {
    // The task set PreFinalize/Ready itself, then failed in a later subtask (UpdateSoftwareVersion,
    // post-upgrade hooks, MarkUniverseUpdateSuccess). The failure handler leaves these states.
    addStateSubTask(SoftwareUpgradeState.Upgrading, true, TaskInfo.State.Success);
    addStateSubTask(SoftwareUpgradeState.PreFinalize, true, TaskInfo.State.Success);
    assertTrue(canRollbackWith(failedUpgrade.getUuid(), SoftwareUpgradeState.PreFinalize, true));
  }

  @Test
  public void testAllowedWhenFailedAfterReadyWithoutFinalize() {
    addStateSubTask(SoftwareUpgradeState.Upgrading, true, TaskInfo.State.Success);
    addStateSubTask(SoftwareUpgradeState.Ready, true, TaskInfo.State.Success);
    assertTrue(canRollbackWith(failedUpgrade.getUuid(), SoftwareUpgradeState.Ready, true));
  }

  @Test
  public void testRejectedWhenStateLeftByEarlierUpgrade() {
    // This task reached Upgrading but the universe state is not one this task wrote.
    addStateSubTask(SoftwareUpgradeState.Upgrading, true, TaskInfo.State.Success);
    assertFalse(canRollbackWith(failedUpgrade.getUuid(), SoftwareUpgradeState.PreFinalize, true));
    assertFalse(canRollbackWith(failedUpgrade.getUuid(), SoftwareUpgradeState.Ready, true));
  }

  @Test
  public void testRejectedWhenEndOfUpgradeStateUpdateFailed() {
    addStateSubTask(SoftwareUpgradeState.Upgrading, true, TaskInfo.State.Success);
    addStateSubTask(SoftwareUpgradeState.PreFinalize, true, TaskInfo.State.Failure);
    assertFalse(canRollbackWith(failedUpgrade.getUuid(), SoftwareUpgradeState.PreFinalize, true));
  }

  @Test
  public void testRejectedWhenStateSetWithoutRollbackAllowed() {
    // E.g. a finalize-style update to Ready that clears the rollback flag.
    addStateSubTask(SoftwareUpgradeState.Ready, false, TaskInfo.State.Success);
    assertFalse(canRollbackWith(failedUpgrade.getUuid(), SoftwareUpgradeState.Ready, true));
  }

  @Test
  public void testAllowedForRetryStoppedBeforeItsOwnUpgrading() {
    // An earlier attempt got past Upgrading and left UpgradeFailed. The retry froze the universe
    // (so it owns it) and stopped before its own Upgrading step, with no state subtask of its own.
    assertTrue(canRollbackWith(failedUpgrade.getUuid(), SoftwareUpgradeState.UpgradeFailed, true));
  }

  @Test
  public void testRejectedWhenRollbackNotAllowed() {
    assertFalse(
        canRollbackWith(failedUpgrade.getUuid(), SoftwareUpgradeState.UpgradeFailed, false));
  }

  @Test
  public void testRejectedWhenTaskDoesNotOwnUniverse() {
    assertFalse(canRollbackWith(UUID.randomUUID(), SoftwareUpgradeState.UpgradeFailed, true));
    assertFalse(canRollbackWith(null, SoftwareUpgradeState.UpgradeFailed, true));
  }

  @Test
  public void testRejectedWhenUniverseMissing() {
    assertFalse(computer.canRollback(failedUpgrade, UUID.randomUUID()));
  }
}
