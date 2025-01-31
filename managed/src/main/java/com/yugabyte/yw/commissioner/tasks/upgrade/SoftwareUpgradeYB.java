// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.tasks.subtasks.ManageCatalogUpgradeSuperUser.Action;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.gflags.AutoFlagUtil;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.UpgradeDetails.YsqlMajorVersionUpgradeState;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Set;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import play.mvc.Http.Status;

/**
 * Use this task to upgrade software yugabyte DB version if universe is already on version greater
 * or equal to 2.20.x
 */
@Slf4j
@Retryable
@Abortable
public class SoftwareUpgradeYB extends SoftwareUpgradeTaskBase {

  private final AutoFlagUtil autoFlagUtil;

  @Inject
  protected SoftwareUpgradeYB(
      BaseTaskDependencies baseTaskDependencies, AutoFlagUtil autoFlagUtil) {
    super(baseTaskDependencies);
    this.autoFlagUtil = autoFlagUtil;
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
    runUpgrade(
        () -> {
          MastersAndTservers nodesToApply = getNodesToBeRestarted();
          Set<NodeDetails> allNodes = toOrderedSet(fetchNodes(taskParams().upgradeOption).asPair());
          boolean requireYsqlMajorVersionUpgrade =
              isYsqlMajorVersionUpgrade(universe, currentVersion, newVersion);
          boolean requireAdditionalSuperUserForCatalogUpgrade =
              isSuperUserRequiredForCatalogUpgrade(universe, currentVersion, newVersion);

          createUpdateUniverseSoftwareUpgradeStateTask(
              UniverseDefinitionTaskParams.SoftwareUpgradeState.Upgrading,
              true /* isSoftwareRollbackAllowed */);

          if (!universe
              .getUniverseDetails()
              .xClusterInfo
              .isSourceRootCertDirPathGflagConfigured()) {
            createXClusterSourceRootCertDirPathGFlagTasks();
          }

          // Download software to nodes which does not have either master or tserver with new
          // version.
          createDownloadTasks(toOrderedSet(nodesToApply.asPair()), newVersion);

          // If any master has been updated to new version, then this step would have been
          // completed and we don't need to do it again.
          if (requireYsqlMajorVersionUpgrade) {
            if (nodesToApply.mastersList.size() == universe.getMasters().size()) {
              // Set ysql_yb_enable_expression_pushdown to false for tservers for ysql major
              // upgrade.
              createGFlagsUpgradeTaskForYSQLMajorUpgrade(
                  universe, YsqlMajorVersionUpgradeState.IN_PROGRESS);
              // Run this pre-check after downloading software as it require pg_upgrade binary.
              createPGUpgradeTServerCheckTask(newVersion);
            }

            if (requireAdditionalSuperUserForCatalogUpgrade) {
              // Create a superuser for ysql catalog upgrade.
              createManageCatalogUpgradeSuperUserTask(Action.CREATE_USER);
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
            if (requireYsqlMajorVersionUpgrade
                && nodesToApply.tserversList.size() == universe.getTServers().size()) {
              createPGUpgradeTServerCheckTask(newVersion);

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

          if (taskParams().installYbc) {
            createYbcInstallTask(
                universe,
                new ArrayList<>(allNodes),
                newVersion,
                requireYsqlMajorVersionUpgrade ? YsqlMajorVersionUpgradeState.IN_PROGRESS : null);
          }

          createCheckSoftwareVersionTask(allNodes, newVersion);

          createStoreAutoFlagConfigVersionTask(taskParams().getUniverseUUID());

          createPromoteAutoFlagTask(
              universe.getUniverseUUID(),
              true /* ignoreErrors*/,
              AutoFlagUtil.LOCAL_VOLATILE_AUTO_FLAG_CLASS_NAME /* maxClass */);

          // Update Software version
          createUpdateSoftwareVersionTask(newVersion, false /*isSoftwareUpdateViaVm*/)
              .setSubTaskGroupType(getTaskSubGroupType());

          if (!taskParams().rollbackSupport) {
            // If rollback is not supported, then finalize the upgrade during this task.
            if (requireYsqlMajorVersionUpgrade) {
              createFinalizeUpgradeTasks(
                  taskParams().upgradeSystemCatalog, getFinalizeYSQLMajorUpgradeTask(universe));
            } else {
              createFinalizeUpgradeTasks(taskParams().upgradeSystemCatalog);
            }
          } else {
            // Check if upgrade require finalize.
            boolean upgradeRequireFinalize =
                checkUpgradeRequireFinalize(currentVersion, newVersion);

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
          if (isSuperUserRequiredForCatalogUpgrade(universe, currentVersion, newVersion)) {
            createManageCatalogUpgradeSuperUserTask(Action.DELETE_PG_PASS_FILE);
          }
        });
  }

  private boolean checkUpgradeRequireFinalize(String currentVersion, String newVersion) {
    try {
      return autoFlagUtil.upgradeRequireFinalize(currentVersion, newVersion);
    } catch (IOException e) {
      log.error("Error: ", e);
      throw new PlatformServiceException(
          Status.INTERNAL_SERVER_ERROR, "Error while checking auto-finalize for upgrade");
    }
  }
}
