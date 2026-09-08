// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.CanRollback;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.KubernetesUpgradeTaskBase;
import com.yugabyte.yw.commissioner.UpgradeTaskBase.UpgradeContext;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.ManageCatalogUpgradeSuperUser.Action;
import com.yugabyte.yw.common.SoftwareUpgradeHelper;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.gflags.AutoFlagUtil;
import com.yugabyte.yw.common.operator.OperatorStatusUpdaterFactory;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.UpgradeDetails.YsqlMajorVersionUpgradeState;
import lombok.extern.slf4j.Slf4j;
import org.yb.master.MasterAdminOuterClass.YsqlMajorCatalogUpgradeState;

@Slf4j
@Abortable
@Retryable
@CanRollback
public class SoftwareKubernetesUpgradeYB extends KubernetesUpgradeTaskBase {

  private final SoftwareUpgradeHelper softwareUpgradeHelper;

  @Inject
  protected SoftwareKubernetesUpgradeYB(
      BaseTaskDependencies baseTaskDependencies,
      SoftwareUpgradeHelper softwareUpgradeHelper,
      OperatorStatusUpdaterFactory operatorStatusUpdaterFactory) {
    super(baseTaskDependencies, operatorStatusUpdaterFactory);
    this.softwareUpgradeHelper = softwareUpgradeHelper;
  }

  @Override
  protected SoftwareUpgradeParams taskParams() {
    return (SoftwareUpgradeParams) taskParams;
  }

  @Override
  public SubTaskGroupType getTaskSubGroupType() {
    return SubTaskGroupType.UpgradingSoftware;
  }

  @Override
  public void validateParams(boolean isFirstTry) {
    super.validateParams(isFirstTry);
    taskParams().verifyParams(getUniverse(), isFirstTry);
  }

  @Override
  protected String getTargetSoftwareVersion() {
    return taskParams().ybSoftwareVersion;
  }

  @Override
  protected void createPrecheckTasks(Universe universe) {
    super.createPrecheckTasks(universe);
    boolean ysqlMajorVersionUpgrade =
        softwareUpgradeHelper.isYsqlMajorVersionUpgradeRequired(
            universe,
            universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
            taskParams().ybSoftwareVersion);
    createSoftwareUpgradePrecheckTasks(taskParams().ybSoftwareVersion, ysqlMajorVersionUpgrade);
    addBasicPrecheckTasks();
  }

  @Override
  public void run() {
    String newVersion = taskParams().ybSoftwareVersion;
    String currentVersion =
        getUniverse().getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
    Universe universe = getUniverse();
    boolean ysqlMajorVersionUpgrade =
        softwareUpgradeHelper.isYsqlMajorVersionUpgradeRequired(
            universe, currentVersion, newVersion);
    boolean requireAdditionalSuperUserForCatalogUpgrade =
        softwareUpgradeHelper.isSuperUserRequiredForCatalogUpgrade(
            universe, currentVersion, newVersion);
    runUpgrade(
        () -> {
          createUpdateUniverseSoftwareUpgradeStateTask(
              UniverseDefinitionTaskParams.SoftwareUpgradeState.Upgrading,
              true /* isSoftwareRollbackAllowed */);

          createStoreAutoFlagConfigVersionTask(taskParams().getUniverseUUID(), newVersion);

          // Disable PITR configs at the start of software upgrade
          createDisablePitrConfigTask();

          String password = null;
          boolean catalogUpgradeCompleted = false;

          if (ysqlMajorVersionUpgrade) {
            boolean rollbackMasters = false;
            if (softwareUpgradeHelper.isAllMasterUpgradedToYsqlMajorVersion(universe, "15")) {
              YsqlMajorCatalogUpgradeState state =
                  softwareUpgradeHelper.getYsqlMajorCatalogUpgradeState(universe);
              log.info(
                  "YSQL catalog upgrade state for universe {}: {}",
                  universe.getUniverseUUID(),
                  state.toString());
              if (requireAdditionalSuperUserForCatalogUpgrade
                  && state.equals(
                      YsqlMajorCatalogUpgradeState.YSQL_MAJOR_CATALOG_UPGRADE_PENDING_ROLLBACK)) {
                log.info(
                    "YSQL catalog upgrade is in a failed state. Rolling back catalog upgrade.");
                createRollbackYsqlMajorVersionCatalogUpgradeTask();
                // Masters stay on PG15 after catalog rollback; roll them to the previous image
                // before CREATE USER so DDLs are allowed again.
                rollbackMasters = true;
              } else if (state.equals(
                  YsqlMajorCatalogUpgradeState
                      .YSQL_MAJOR_CATALOG_UPGRADE_PENDING_FINALIZE_OR_ROLLBACK)) {
                catalogUpgradeCompleted = true;
              } else {
                log.info(
                    "YSQL catalog upgrade is in a pending state. Proceeding with all upgrade"
                        + " subtasks.");
              }
            } else if (requireAdditionalSuperUserForCatalogUpgrade
                && softwareUpgradeHelper.isAnyMasterUpgradedOrInProgressForYsqlMajorVersion(
                    universe, "15")) {
              // Partial master upgrade (e.g. abort after first master): PG15 catalog leader blocks
              // DDLs needed to create yugabyte_upgrade. Match VM recovery by rolling masters back.
              rollbackMasters = true;
            }

            if (!catalogUpgradeCompleted) {
              if (rollbackMasters) {
                log.info(
                    "Rolling back master before upgrade to enable DDLs to create upgrade user.");
                createUpgradeTask(
                    getUniverse(),
                    currentVersion,
                    true /* upgradeMasters */,
                    false /* upgradeTservers */,
                    taskParams().isEnableYbc(),
                    confGetter.getGlobalConf(GlobalConfKeys.ybcStableVersion),
                    getSoftwareUpgradeContext(
                        currentVersion, YsqlMajorVersionUpgradeState.ROLLBACK_IN_PROGRESS));
              }

              // Set ysql_yb_major_version_upgrade_compatibility to 11 for masters/tservers for
              // ysql major upgrade (non-restart helm). Master image revert on retry is handled by
              // the rolling createUpgradeTask above when mixed/failed masters are detected.
              createGFlagsUpgradeAndUpdateMastersTaskForYSQLMajorUpgrade(
                  universe, currentVersion, YsqlMajorVersionUpgradeState.IN_PROGRESS);

              if (requireAdditionalSuperUserForCatalogUpgrade) {
                password = Util.getPostgresCompatiblePassword();
                createManageCatalogUpgradeSuperUserTask(Action.CREATE_USER, password);
              }
            }
          }

          // Create Kubernetes Upgrade Task
          if (!catalogUpgradeCompleted) {
            createUpgradeTask(
                getUniverse(),
                taskParams().ybSoftwareVersion,
                true,
                false,
                taskParams().isEnableYbc(),
                confGetter.getGlobalConf(GlobalConfKeys.ybcStableVersion),
                getSoftwareUpgradeContext(
                    newVersion,
                    ysqlMajorVersionUpgrade ? YsqlMajorVersionUpgradeState.IN_PROGRESS : null));
          }

          if (ysqlMajorVersionUpgrade) {
            createUpdateSoftwareUpdatePrevConfigTask(
                true /* canRollbackCatalogUpgrade */,
                false /* allTserversUpgradedToYsqlMajorVersion */);
          }

          if (ysqlMajorVersionUpgrade && !catalogUpgradeCompleted) {

            if (password != null) {
              createManageCatalogUpgradeSuperUserTask(Action.CREATE_PG_PASS_FILE, password);
            }

            createRunYsqlMajorVersionCatalogUpgradeTask();

            if (requireAdditionalSuperUserForCatalogUpgrade) {
              // Delete the pg_pass file after catalog upgrade.
              createManageCatalogUpgradeSuperUserTask(Action.DELETE_PG_PASS_FILE);
            }
          }

          createUpgradeTask(
              getUniverse(),
              taskParams().ybSoftwareVersion,
              true,
              true,
              taskParams().isEnableYbc(),
              confGetter.getGlobalConf(GlobalConfKeys.ybcStableVersion),
              getSoftwareUpgradeContext(
                  newVersion,
                  ysqlMajorVersionUpgrade ? YsqlMajorVersionUpgradeState.IN_PROGRESS : null));

          if (ysqlMajorVersionUpgrade) {
            createUpdateSoftwareUpdatePrevConfigTask(
                true /* canRollbackCatalogUpgrade */,
                true /* allTserversUpgradedToYsqlMajorVersion */);
          }

          if (ysqlMajorVersionUpgrade) {
            createGFlagsUpgradeAndUpdateMastersTaskForYSQLMajorUpgrade(
                universe,
                taskParams().ybSoftwareVersion,
                YsqlMajorVersionUpgradeState.UPGRADE_COMPLETE);
          }

          createPromoteAutoFlagTask(
              taskParams().getUniverseUUID(),
              true /* ignoreErrors*/,
              AutoFlagUtil.LOCAL_VOLATILE_AUTO_FLAG_CLASS_NAME /* maxClass */);

          if (taskParams().isEnableYbc()) {
            createUpdateYbcTask(taskParams().getYbcSoftwareVersion())
                .setSubTaskGroupType(getTaskSubGroupType());
          }

          if (universe.getUniverseDetails().useNewHelmNamingStyle) {
            createPodDisruptionBudgetPolicyTask(false /* deletePDB */)
                .setSubTaskGroupType(getTaskSubGroupType());
          }

          if (!taskParams().rollbackSupport) {
            createFinalizeUpgradeTasks(
                taskParams().upgradeSystemCatalog,
                ysqlMajorVersionUpgrade,
                requireAdditionalSuperUserForCatalogUpgrade);
          } else {
            boolean upgradeRequireFinalize =
                softwareUpgradeHelper.checkUpgradeRequireFinalize(currentVersion, newVersion);
            if (upgradeRequireFinalize) {
              createUpdateUniverseSoftwareUpgradeStateTask(
                  UniverseDefinitionTaskParams.SoftwareUpgradeState.PreFinalize,
                  true /* isSoftwareRollbackAllowed */);
            } else {
              createUpdateUniverseSoftwareUpgradeStateTask(
                  UniverseDefinitionTaskParams.SoftwareUpgradeState.Ready,
                  true /* isSoftwareRollbackAllowed */);
            }
          }
          // Also idempotent can be run again here.
          // Mark the final software version on the universe (last subtask).
          createUpdateSoftwareVersionTask(taskParams().ybSoftwareVersion)
              .setSubTaskGroupType(getTaskSubGroupType());
        },
        () -> {
          if (requireAdditionalSuperUserForCatalogUpgrade) {
            createManageCatalogUpgradeSuperUserTask(Action.DELETE_PG_PASS_FILE);
          }
        });
  }

  protected UpgradeContext getSoftwareUpgradeContext(
      String targetSoftwareVersion, YsqlMajorVersionUpgradeState ysqlMajorVersionUpgrade) {
    return UpgradeContext.builder()
        .targetSoftwareVersion(targetSoftwareVersion)
        .ysqlMajorVersionUpgradeState(ysqlMajorVersionUpgrade)
        .build();
  }
}
