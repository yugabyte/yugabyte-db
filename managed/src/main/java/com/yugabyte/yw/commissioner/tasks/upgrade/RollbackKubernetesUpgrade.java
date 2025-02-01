// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.KubernetesUpgradeTaskBase;
import com.yugabyte.yw.commissioner.UpgradeTaskBase.UpgradeContext;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.tasks.subtasks.ManageCatalogUpgradeSuperUser.Action;
import com.yugabyte.yw.common.SoftwareUpgradeHelper;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.operator.OperatorStatusUpdaterFactory;
import com.yugabyte.yw.forms.RollbackUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.UpgradeDetails.YsqlMajorVersionUpgradeState;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Abortable
@Retryable
public class RollbackKubernetesUpgrade extends KubernetesUpgradeTaskBase {

  private final SoftwareUpgradeHelper softwareUpgradeHelper;

  @Inject
  protected RollbackKubernetesUpgrade(
      BaseTaskDependencies baseTaskDependencies,
      SoftwareUpgradeHelper softwareUpgradeHelper,
      OperatorStatusUpdaterFactory operatorStatusUpdaterFactory) {
    super(baseTaskDependencies, operatorStatusUpdaterFactory);
    this.softwareUpgradeHelper = softwareUpgradeHelper;
  }

  @Override
  protected RollbackUpgradeParams taskParams() {
    return (RollbackUpgradeParams) taskParams;
  }

  @Override
  public UserTaskDetails.SubTaskGroupType getTaskSubGroupType() {
    return UserTaskDetails.SubTaskGroupType.RollingBackSoftware;
  }

  public NodeDetails.NodeState getNodeState() {
    return NodeDetails.NodeState.RollbackUpgrade;
  }

  @Override
  protected String getTargetSoftwareVersion() {
    Universe universe = getUniverse();
    UniverseDefinitionTaskParams.PrevYBSoftwareConfig prevYBSoftwareConfig =
        universe.getUniverseDetails().prevYBSoftwareConfig;
    String newVersion =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
    if (prevYBSoftwareConfig != null
        && !newVersion.equals(prevYBSoftwareConfig.getSoftwareVersion())) {
      newVersion = prevYBSoftwareConfig.getSoftwareVersion();
    }
    return newVersion;
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          Universe universe = getUniverse();

          createUpdateUniverseSoftwareUpgradeStateTask(
              UniverseDefinitionTaskParams.SoftwareUpgradeState.RollingBack);

          UniverseDefinitionTaskParams.PrevYBSoftwareConfig prevYBSoftwareConfig =
              universe.getUniverseDetails().prevYBSoftwareConfig;
          String targetVersion =
              universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
          String currentVersion = targetVersion;
          // Skip auto flags restore incase upgrade did not take place or succeed.
          if (prevYBSoftwareConfig != null
              && !targetVersion.equals(prevYBSoftwareConfig.getSoftwareVersion())) {
            targetVersion = prevYBSoftwareConfig.getSoftwareVersion();
            int autoFlagConfigVersion = prevYBSoftwareConfig.getAutoFlagConfigVersion();
            // Restore old auto flag Config
            createRollbackAutoFlagTask(taskParams().getUniverseUUID(), autoFlagConfigVersion);
          }

          boolean ysqlMajorVersionUpgrade =
              softwareUpgradeHelper.isYsqlMajorVersionUpgradeRequired(
                  universe, targetVersion, currentVersion);
          boolean requireAdditionalSuperUserForCatalogUpgrade =
              softwareUpgradeHelper.isSuperUserRequiredForCatalogUpgrade(
                  universe, targetVersion, currentVersion);

          // Create Kubernetes Upgrade Task
          createUpgradeTask(
              getUniverse(),
              targetVersion,
              false,
              true,
              taskParams().isEnableYbc(),
              confGetter.getGlobalConf(GlobalConfKeys.ybcStableVersion),
              getRollbackUpgradeContext(
                  targetVersion,
                  ysqlMajorVersionUpgrade
                      ? YsqlMajorVersionUpgradeState.ROLLBACK_IN_PROGRESS
                      : null));

          if (ysqlMajorVersionUpgrade
              && softwareUpgradeHelper.isAllMasterUpgradedToYsqlMajorVersion(universe, "15")) {
            createRollbackYsqlMajorVersionCatalogUpgradeTask();
          }

          // Create Kubernetes Upgrade Task
          createUpgradeTask(
              getUniverse(),
              targetVersion,
              true,
              true,
              taskParams().isEnableYbc(),
              confGetter.getGlobalConf(GlobalConfKeys.ybcStableVersion),
              getRollbackUpgradeContext(
                  targetVersion,
                  ysqlMajorVersionUpgrade
                      ? YsqlMajorVersionUpgradeState.ROLLBACK_IN_PROGRESS
                      : null));

          if (ysqlMajorVersionUpgrade) {
            // Un-set the flag ysql_yb_major_version_upgrade_compatibility as major version upgrade
            // is
            // rolled back.
            createGFlagsUpgradeAndRollbackMastersTaskForYSQLMajorUpgrade(
                universe,
                getTargetSoftwareVersion(),
                YsqlMajorVersionUpgradeState.ROLLBACK_COMPLETE);

            if (requireAdditionalSuperUserForCatalogUpgrade) {
              createManageCatalogUpgradeSuperUserTask(Action.DELETE_USER);
            }
          }

          // Update Software version
          createUpdateSoftwareVersionTask(targetVersion, false /*isSoftwareUpdateViaVm*/)
              .setSubTaskGroupType(getTaskSubGroupType());

          createUpdateUniverseSoftwareUpgradeStateTask(
              UniverseDefinitionTaskParams.SoftwareUpgradeState.Ready,
              false /* isSoftwareRollbackAllowed */);
        });
  }

  private UpgradeContext getRollbackUpgradeContext(
      String targetSoftwareVersion, YsqlMajorVersionUpgradeState ysqlMajorVersionUpgradeState) {
    return UpgradeContext.builder()
        .reconfigureMaster(false)
        .runBeforeStopping(false)
        .processInactiveMaster(true)
        .processTServersFirst(true)
        .targetSoftwareVersion(targetSoftwareVersion)
        .ysqlMajorVersionUpgradeState(ysqlMajorVersionUpgradeState)
        .build();
  }
}
