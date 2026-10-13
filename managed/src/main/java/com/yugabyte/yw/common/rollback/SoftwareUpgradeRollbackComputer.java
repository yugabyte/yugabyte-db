// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.rollback;

import com.yugabyte.yw.controllers.handlers.UpgradeUniverseHandler;
import com.yugabyte.yw.forms.RollbackUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.SoftwareUpgradeState;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.UUID;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

/**
 * Builds a software-upgrade downgrade submission from a failed SoftwareUpgradeYB /
 * SoftwareKubernetesUpgradeYB task.
 *
 * <p>Must run as a fresh task (no previousTaskUUID) so it does not inherit the failed upgrade's
 * runtimeInfo or get treated as a retry of that upgrade.
 */
@Singleton
@Slf4j
public class SoftwareUpgradeRollbackComputer implements TaskRollbackComputer {

  private final UpgradeUniverseHandler upgradeUniverseHandler;

  @Inject
  public SoftwareUpgradeRollbackComputer(UpgradeUniverseHandler upgradeUniverseHandler) {
    this.upgradeUniverseHandler = upgradeUniverseHandler;
  }

  /**
   * Only an upgrade that got past its {@code Upgrading} step has anything to roll back. The failed
   * task must still own the universe with {@code isSoftwareRollbackAllowed}, and the universe must
   * be in one of these states:
   *
   * <ul>
   *   <li>{@code UpgradeFailed}: the failure handler sets it only for an in-progress upgrade.
   *   <li>{@code PreFinalize} or {@code Ready}, set by this task's own end-of-upgrade state update.
   *       Subtasks still run after that update, and the failure handler leaves these states as they
   *       are. The same state left by an earlier upgrade does not count: rolling back would
   *       downgrade that upgrade.
   * </ul>
   *
   * <p>A task that stopped earlier (in prechecks, or after the freeze but before {@code Upgrading})
   * upgraded no node, so it is refused and stays retryable.
   */
  @Override
  public boolean canRollback(TaskInfo failedTask, UUID universeUUID) {
    return Universe.maybeGet(universeUUID)
        .map(Universe::getUniverseDetails)
        .map(details -> canRollback(failedTask, details))
        .orElse(false);
  }

  private static boolean canRollback(TaskInfo failedTask, UniverseDefinitionTaskParams details) {
    boolean ownsUniverse =
        failedTask.getUuid().equals(details.updatingTaskUUID)
            || failedTask.getUuid().equals(details.placementModificationTaskUuid);
    if (!ownsUniverse || !details.isSoftwareRollbackAllowed) {
      return false;
    }
    switch (details.softwareUpgradeState) {
      case UpgradeFailed:
        return true;
      case PreFinalize:
      case Ready:
        return setRollbackAllowedState(failedTask, details.softwareUpgradeState);
      default:
        return false;
    }
  }

  /** Whether a subtask of {@code task} set {@code state} with rollback allowed, and succeeded. */
  private static boolean setRollbackAllowedState(TaskInfo task, SoftwareUpgradeState state) {
    return TaskInfo.find
        .query()
        .where()
        .eq("parent_uuid", task.getUuid())
        .eq("task_type", TaskType.UpdateUniverseState)
        .eq("task_state", TaskInfo.State.Success)
        .findList()
        .stream()
        .map(TaskInfo::getTaskParams)
        .anyMatch(
            params ->
                state.name().equals(params.path("state").asText())
                    && params.path("isSoftwareRollbackAllowed").asBoolean(false));
  }

  @Override
  public RollbackSubmission compute(RollbackContext context) {
    Customer customer = context.getCustomer();
    CustomerTask customerTask = context.getCustomerTask();
    Universe universe = Universe.getOrBadRequest(customerTask.getTargetUUID(), customer);
    RollbackUpgradeParams rollbackParams =
        Json.fromJson(context.getOldTaskParams(), RollbackUpgradeParams.class);
    // Skip the version check for this programmatically-submitted task.
    rollbackParams.expectedUniverseVersion = -1;
    TaskType rollbackTaskType =
        upgradeUniverseHandler.prepareRollbackUpgrade(rollbackParams, universe);
    log.info(
        "Prepared rollback (downgrade) for failed software upgrade task {} on {}:{}, rollback"
            + " type={}.",
        context.getTaskInfo().getUuid(),
        customerTask.getTargetUUID(),
        customerTask.getTargetName(),
        rollbackTaskType);
    return new RollbackSubmission(
        rollbackTaskType,
        rollbackParams,
        CustomerTask.TaskType.RollbackUpgrade,
        false /* setPreviousTaskUUID */);
  }
}
