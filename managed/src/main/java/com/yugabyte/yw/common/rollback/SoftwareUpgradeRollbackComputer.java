// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.rollback;

import com.yugabyte.yw.controllers.handlers.UpgradeUniverseHandler;
import com.yugabyte.yw.forms.RollbackUpgradeParams;
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
   * Only an upgrade that got past its {@code Upgrading} step has anything to roll back: that step
   * sets {@code isSoftwareRollbackAllowed}, and the failure handler moves only an in-progress
   * upgrade to {@code UpgradeFailed}. A task that stopped earlier (in prechecks, or after the
   * freeze but before that step) upgraded no node, and the universe still describes the previous
   * upgrade, so a rollback would fail or downgrade that upgrade. The failed task must also still
   * own the universe, as listing requires.
   */
  @Override
  public boolean canRollback(TaskInfo failedTask, UUID universeUUID) {
    return Universe.maybeGet(universeUUID)
        .map(Universe::getUniverseDetails)
        .map(
            details ->
                (failedTask.getUuid().equals(details.updatingTaskUUID)
                        || failedTask.getUuid().equals(details.placementModificationTaskUuid))
                    && details.softwareUpgradeState == SoftwareUpgradeState.UpgradeFailed
                    && details.isSoftwareRollbackAllowed)
        .orElse(false);
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
