// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.common.SoftwareUpgradeHelper;
import com.yugabyte.yw.forms.FinalizeUpgradeParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Collections;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Abortable
@Retryable
public class FinalizeUpgrade extends SoftwareUpgradeTaskBase {

  private final SoftwareUpgradeHelper softwareUpgradeHelper;

  @Inject
  protected FinalizeUpgrade(
      BaseTaskDependencies baseTaskDependencies, SoftwareUpgradeHelper softwareUpgradeHelper) {
    super(baseTaskDependencies, softwareUpgradeHelper);
    this.softwareUpgradeHelper = softwareUpgradeHelper;
  }

  @Override
  public UserTaskDetails.SubTaskGroupType getTaskSubGroupType() {
    return UserTaskDetails.SubTaskGroupType.FinalizingUpgrade;
  }

  @Override
  public NodeDetails.NodeState getNodeState() {
    return NodeDetails.NodeState.FinalizeUpgrade;
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
  protected MastersAndTservers calculateNodesToBeRestarted() {
    return new MastersAndTservers(Collections.emptyList(), Collections.emptyList());
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
