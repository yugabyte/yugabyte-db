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
import org.apache.commons.lang3.StringUtils;

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
          boolean ysqlMajorVersionUpgrade = false;
          boolean requireAdditionalSuperUserForCatalogUpgrade = false;
          String currentVersion =
              universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
          if (prevYBSoftwareConfig != null) {
            targetVersion = prevYBSoftwareConfig.getSoftwareVersion();
            if (!StringUtils.isEmpty(prevYBSoftwareConfig.getTargetUpgradeSoftwareVersion())) {
              currentVersion = prevYBSoftwareConfig.getTargetUpgradeSoftwareVersion();
            }

            ysqlMajorVersionUpgrade =
                softwareUpgradeHelper.isYsqlMajorVersionUpgradeRequired(
                    universe, targetVersion, currentVersion);
            requireAdditionalSuperUserForCatalogUpgrade =
                softwareUpgradeHelper.isSuperUserRequiredForCatalogUpgrade(
                    universe, targetVersion, currentVersion);

            int autoFlagConfigVersion = prevYBSoftwareConfig.getAutoFlagConfigVersion();
            // Restore old auto flag Config
            createRollbackAutoFlagTask(taskParams().getUniverseUUID(), autoFlagConfigVersion);
          }

          if (ysqlMajorVersionUpgrade) {
            // Set the flag ysql_yb_major_version_upgrade_compatibility as major version upgrade is
            // rolled back.
            if (prevYBSoftwareConfig != null
                && prevYBSoftwareConfig.isAllTserversUpgradedToYsqlMajorVersion()) {
              // Only set the ysql_yb_major_version_upgrade_compatibility flag if all tservers
              // were successfully upgraded to the target YSQL major version. Otherwise,
              // the flag will be already be set due to the upgrade failure.
              createGFlagsUpgradeAndUpdateMastersTaskForYSQLMajorUpgrade(
                  universe, currentVersion, YsqlMajorVersionUpgradeState.ROLLBACK_IN_PROGRESS);
            }
          }

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
              && prevYBSoftwareConfig != null
              && prevYBSoftwareConfig.isCanRollbackCatalogUpgrade()) {
            createRollbackYsqlMajorVersionCatalogUpgradeTask();
            createUpdateSoftwareUpdatePrevConfigTask(
                false /* canRollbackCatalogUpgrade */,
                false /* allTserversUpgradedToYsqlMajorVersion */);
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
            createGFlagsUpgradeAndUpdateMastersTaskForYSQLMajorUpgrade(
                universe, targetVersion, YsqlMajorVersionUpgradeState.ROLLBACK_COMPLETE);

            createCleanUpPGUpgradeDataDirTask();

            if (requireAdditionalSuperUserForCatalogUpgrade) {
              createManageCatalogUpgradeSuperUserTask(Action.DELETE_USER);
            }

            // Update PITR configs to set intermittentMinRecoverTimeInMillis to current time
            // as PITR configs are only valid from the completion of software rollback
            createUpdatePitrConfigIntermittentMinRecoverTimeTask();
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
