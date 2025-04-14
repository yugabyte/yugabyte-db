// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.tasks.subtasks.ManageCatalogUpgradeSuperUser.Action;
import com.yugabyte.yw.common.SoftwareUpgradeHelper;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.gflags.AutoFlagUtil;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.UpgradeDetails.YsqlMajorVersionUpgradeState;
import java.util.ArrayList;
import java.util.Set;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.master.MasterAdminOuterClass.YsqlMajorCatalogUpgradeState;

/**
 * Use this task to upgrade software yugabyte DB version if universe is already on version greater
 * or equal to 2.20.x
 */
@Slf4j
@Retryable
@Abortable
public class SoftwareUpgradeYB extends SoftwareUpgradeTaskBase {

  private final SoftwareUpgradeHelper softwareUpgradeHelper;

  @Inject
  protected SoftwareUpgradeYB(
      BaseTaskDependencies baseTaskDependencies, SoftwareUpgradeHelper softwareUpgradeHelper) {
    super(baseTaskDependencies, softwareUpgradeHelper);
    this.softwareUpgradeHelper = softwareUpgradeHelper;
  }

  public NodeState getNodeState() {
    return NodeState.UpgradeSoftware;
  }

  @Override
  protected SoftwareUpgradeParams taskParams() {
    return (SoftwareUpgradeParams) taskParams;
  }

  @Override
  public void validateParams(boolean isFirstTry) {
    super.validateParams(isFirstTry);
    taskParams().verifyParams(getUniverse(), isFirstTry);
  }

  @Override
  protected void createPrecheckTasks(Universe universe) {
    createPrecheckTasks(universe, taskParams().ybSoftwareVersion);
  }

  @Override
  protected MastersAndTservers calculateNodesToBeRestarted() {
    String newVersion = taskParams().ybSoftwareVersion;
    MastersAndTservers allNodes = fetchNodes(taskParams().upgradeOption);
    return filterOutAlreadyProcessedNodes(getUniverse(), allNodes, newVersion);
  }

  @Override
  protected String getTargetSoftwareVersion() {
    return taskParams().ybSoftwareVersion;
  }

  @Override
  public void run() {
    Universe universe = getUniverse();
    String newVersion = taskParams().ybSoftwareVersion;
    String currentVersion =
        getUniverse().getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
    boolean requireYsqlMajorVersionUpgrade =
        softwareUpgradeHelper.isYsqlMajorVersionUpgradeRequired(
            universe, currentVersion, newVersion);
    boolean requireAdditionalSuperUserForCatalogUpgrade =
        softwareUpgradeHelper.isSuperUserRequiredForCatalogUpgrade(
            universe, currentVersion, newVersion);
    runUpgrade(
        () -> {
          MastersAndTservers nodesToApply = getNodesToBeRestarted();
          Set<NodeDetails> allNodes = toOrderedSet(fetchNodes(taskParams().upgradeOption).asPair());

          createUpdateUniverseSoftwareUpgradeStateTask(
              UniverseDefinitionTaskParams.SoftwareUpgradeState.Upgrading,
              true /* isSoftwareRollbackAllowed */);

          if (!universe
              .getUniverseDetails()
              .xClusterInfo
              .isSourceRootCertDirPathGflagConfigured()) {
            createXClusterSourceRootCertDirPathGFlagTasks();
          }

          createStoreAutoFlagConfigVersionTask(taskParams().getUniverseUUID(), newVersion);

          boolean rollbackMaster = false;
          YsqlMajorCatalogUpgradeState catalogUpgradeState = null;
          if (requireAdditionalSuperUserForCatalogUpgrade) {
            if (softwareUpgradeHelper.isAllMasterUpgradedToYsqlMajorVersion(universe, "15")) {
              catalogUpgradeState = softwareUpgradeHelper.getYsqlMajorCatalogUpgradeState(universe);
              if (catalogUpgradeState.equals(
                  YsqlMajorCatalogUpgradeState.YSQL_MAJOR_CATALOG_UPGRADE_PENDING_ROLLBACK)) {
                log.info(
                    "YSQL catalog upgrade is in a failed state. Rolling back catalog upgrade.");
                createRollbackYsqlMajorVersionCatalogUpgradeTask();
                rollbackMaster = true;
              }
            } else if (softwareUpgradeHelper.isAnyMasterUpgradedOrInProgressForYsqlMajorVersion(
                universe, "15")) {
              rollbackMaster = true;
            }
          }

          if (rollbackMaster) {
            log.info("Rolling back master before upgrade to enable DDLs to create upgrade user.");
            createDownloadTasks(universe.getMasters(), currentVersion);
            createMasterUpgradeFlowTasks(
                universe,
                universe.getMasters(),
                currentVersion,
                getUpgradeContext(currentVersion),
                YsqlMajorVersionUpgradeState.ROLLBACK_IN_PROGRESS,
                true // activeRole
                );
            nodesToApply = new MastersAndTservers(universe.getMasters(), universe.getTServers());
          }

          // Download software to nodes which does not have either master or tserver with new
          // version.
          createDownloadTasks(toOrderedSet(nodesToApply.asPair()), newVersion);

          // If any master has been updated to new version, then this step would have been
          // completed and we don't need to do it again.
          if (requireYsqlMajorVersionUpgrade) {
            if (nodesToApply.mastersList.size() == universe.getMasters().size()) {
              // Set ysql_yb_major_version_upgrade_compatibility to 11 for tservers for ysql major
              // upgrade.
              createGFlagsUpgradeTaskForYSQLMajorUpgrade(
                  universe, YsqlMajorVersionUpgradeState.IN_PROGRESS);
            }

            if (requireAdditionalSuperUserForCatalogUpgrade
                && nodesToApply.tserversList.size() == universe.getTServers().size()) {
              // Create a superuser and pgpass file for ysql catalog upgrade.
              createManageCatalogUpgradeSuperUserTask(
                  Action.CREATE_USER_AND_PG_PASS_FILE, Util.getPostgresCompatiblePassword());
            }
          }

          if (nodesToApply.mastersList.size() > 0) {
            createMasterUpgradeFlowTasks(
                universe,
                getNonMasterNodes(nodesToApply.mastersList, nodesToApply.tserversList),
                newVersion,
                getUpgradeContext(taskParams().ybSoftwareVersion),
                requireYsqlMajorVersionUpgrade ? YsqlMajorVersionUpgradeState.IN_PROGRESS : null,
                false /* activeRole */);

            createMasterUpgradeFlowTasks(
                universe,
                nodesToApply.mastersList,
                newVersion,
                getUpgradeContext(taskParams().ybSoftwareVersion),
                requireYsqlMajorVersionUpgrade ? YsqlMajorVersionUpgradeState.IN_PROGRESS : null,
                true /* activeRole */);
          }

          if (nodesToApply.tserversList.size() == universe.getTServers().size()) {
            // If any tservers is upgraded, then we can assume pg upgrade is completed.
            if (requireYsqlMajorVersionUpgrade) {
              createRunYsqlMajorVersionCatalogUpgradeTask();

              if (requireAdditionalSuperUserForCatalogUpgrade) {
                // Delete the pg_pass file after catalog upgrade.
                createManageCatalogUpgradeSuperUserTask(Action.DELETE_PG_PASS_FILE);
              }
            }
          }

          if (nodesToApply.tserversList.size() > 0) {
            createTServerUpgradeFlowTasks(
                universe,
                nodesToApply.tserversList,
                newVersion,
                getUpgradeContext(taskParams().ybSoftwareVersion),
                taskParams().installYbc
                    && !Util.isOnPremManualProvisioning(universe)
                    && universe.getUniverseDetails().getPrimaryCluster().userIntent.useSystemd,
                requireYsqlMajorVersionUpgrade ? YsqlMajorVersionUpgradeState.IN_PROGRESS : null);
          }

          if (requireYsqlMajorVersionUpgrade) {
            // Un-set ysql_yb_major_version_upgrade_compatibility to 0 for tserver after upgrade.
            createGFlagsUpgradeTaskForYSQLMajorUpgrade(
                universe, YsqlMajorVersionUpgradeState.UPGRADE_COMPLETE);
          }

          if (taskParams().installYbc) {
            createYbcInstallTask(universe, new ArrayList<>(allNodes), newVersion);
          }

          createCheckSoftwareVersionTask(allNodes, newVersion);

          createPromoteAutoFlagTask(
              universe.getUniverseUUID(),
              true /* ignoreErrors*/,
              AutoFlagUtil.LOCAL_VOLATILE_AUTO_FLAG_CLASS_NAME /* maxClass */);

          // Update Software version
          createUpdateSoftwareVersionTask(newVersion, false /*isSoftwareUpdateViaVm*/)
              .setSubTaskGroupType(getTaskSubGroupType());

          if (!taskParams().rollbackSupport) {
            // If rollback is not supported, then finalize the upgrade during this task.
            createFinalizeUpgradeTasks(
                taskParams().upgradeSystemCatalog,
                requireYsqlMajorVersionUpgrade,
                requireAdditionalSuperUserForCatalogUpgrade);
          } else {
            // Check if upgrade require finalize.
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
        },
        null /* firstRunTxnCallback */,
        () -> {
          if (requireAdditionalSuperUserForCatalogUpgrade) {
            createManageCatalogUpgradeSuperUserTask(Action.DELETE_PG_PASS_FILE);
          }
        });
  }
}
