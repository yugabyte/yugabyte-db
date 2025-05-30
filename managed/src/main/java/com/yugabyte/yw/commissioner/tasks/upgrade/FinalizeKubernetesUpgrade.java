// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.KubernetesUpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.SoftwareUpgradeHelper;
import com.yugabyte.yw.common.operator.OperatorStatusUpdaterFactory;
import com.yugabyte.yw.forms.FinalizeUpgradeParams;
import com.yugabyte.yw.models.Universe;

@Abortable
@Retryable
public class FinalizeKubernetesUpgrade extends KubernetesUpgradeTaskBase {

  private final SoftwareUpgradeHelper softwareUpgradeHelper;

  @Inject
  protected FinalizeKubernetesUpgrade(
      BaseTaskDependencies baseTaskDependencies,
      OperatorStatusUpdaterFactory operatorStatusUpdaterFactory,
      SoftwareUpgradeHelper softwareUpgradeHelper) {
    super(baseTaskDependencies, operatorStatusUpdaterFactory);
    this.softwareUpgradeHelper = softwareUpgradeHelper;
  }

  @Override
  public SubTaskGroupType getTaskSubGroupType() {
    return SubTaskGroupType.FinalizingUpgrade;
  }

  @Override
  public void validateParams(boolean isFirstTry) {
    super.validateParams(isFirstTry);
    taskParams().verifyParams(getUniverse(), isFirstTry);
  }

  @Override
  protected FinalizeUpgradeParams taskParams() {
    return (FinalizeUpgradeParams) taskParams;
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          Universe universe = getUniverse();
          String currentVersion =
              universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
          String oldVersion =
              universe.getUniverseDetails().prevYBSoftwareConfig.getSoftwareVersion();
          boolean finalizeYSQLMajorVersionUpgrade =
              softwareUpgradeHelper.isYsqlMajorVersionUpgradeRequired(
                  universe, oldVersion, currentVersion);
          boolean superUserRequiredForCatalogUpgrade =
              softwareUpgradeHelper.isSuperUserRequiredForCatalogUpgrade(
                  universe, oldVersion, currentVersion);
          createFinalizeUpgradeTasks(
              taskParams().upgradeSystemCatalog,
              finalizeYSQLMajorVersionUpgrade,
              superUserRequiredForCatalogUpgrade);
        });
  }
}
