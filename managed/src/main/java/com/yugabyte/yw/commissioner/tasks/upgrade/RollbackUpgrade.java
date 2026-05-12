// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.ManageCatalogUpgradeSuperUser.Action;
import com.yugabyte.yw.common.SoftwareUpgradeHelper;
import com.yugabyte.yw.forms.RollbackUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.UpgradeDetails.YsqlMajorVersionUpgradeState;
import java.util.Set;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

@Slf4j
@Abortable
@Retryable
public class RollbackUpgrade extends SoftwareUpgradeTaskBase {

  private final SoftwareUpgradeHelper softwareUpgradeHelper;

  @Inject
  protected RollbackUpgrade(
      BaseTaskDependencies baseTaskDependencies, SoftwareUpgradeHelper softwareUpgradeHelper) {
    super(baseTaskDependencies, softwareUpgradeHelper);
    this.softwareUpgradeHelper = softwareUpgradeHelper;
  }

  @Override
  protected RollbackUpgradeParams taskParams() {
    return (RollbackUpgradeParams) taskParams;
  }

  @Override
  public SubTaskGroupType getTaskSubGroupType() {
    return SubTaskGroupType.RollingBackSoftware;
  }

  @Override
  public void validateParams(boolean isFirstTry) {
    super.validateParams(isFirstTry);
    taskParams().verifyParams(getUniverse(), isFirstTry);
  }

  public NodeState getNodeState() {
    return NodeState.RollbackUpgrade;
  }

  @Override
  protected MastersAndTservers calculateNodesToBeRestarted() {
    Universe universe = getUniverse();

    MastersAndTservers nodes = fetchNodes(taskParams().upgradeOption);
    return filterOutAlreadyProcessedNodes(universe, nodes, getTargetSoftwareVersion());
  }

  @Override
  protected String getTargetSoftwareVersion() {
    Universe universe = getUniverse();
    UniverseDefinitionTaskParams.PrevYBSoftwareConfig prevYBSoftwareConfig =
        universe.getUniverseDetails().prevYBSoftwareConfig;
    String version = universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
    if (prevYBSoftwareConfig != null
        && !version.equals(prevYBSoftwareConfig.getSoftwareVersion())) {
      version = prevYBSoftwareConfig.getSoftwareVersion();
    }
    return version;
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          MastersAndTservers nodes = getNodesToBeRestarted();
          Set<NodeDetails> allNodes = toOrderedSet(fetchNodes(taskParams().upgradeOption).asPair());
          Universe universe = getUniverse();

          UniverseDefinitionTaskParams.PrevYBSoftwareConfig prevYBSoftwareConfig =
              universe.getUniverseDetails().prevYBSoftwareConfig;

          createUpdateUniverseSoftwareUpgradeStateTask(
              UniverseDefinitionTaskParams.SoftwareUpgradeState.RollingBack);

          boolean ysqlMajorVersionUpgrade = false;
          boolean requireAdditionalSuperUserForCatalogUpgrade = false;
          String oldVersion =
              universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;

          if (prevYBSoftwareConfig != null) {
            oldVersion = prevYBSoftwareConfig.getSoftwareVersion();
            String newVersion =
                universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
            if (!StringUtils.isEmpty(prevYBSoftwareConfig.getTargetUpgradeSoftwareVersion())) {
              newVersion = prevYBSoftwareConfig.getTargetUpgradeSoftwareVersion();
            }

            ysqlMajorVersionUpgrade =
                softwareUpgradeHelper.isYsqlMajorVersionUpgradeRequired(
                    universe, oldVersion, newVersion);
            requireAdditionalSuperUserForCatalogUpgrade =
                softwareUpgradeHelper.isSuperUserRequiredForCatalogUpgrade(
                    universe, oldVersion, newVersion);

            int autoFlagConfigVersion = prevYBSoftwareConfig.getAutoFlagConfigVersion();
            // Restore old auto flag Config
            createRollbackAutoFlagTask(taskParams().getUniverseUUID(), autoFlagConfigVersion);
          }

          // Download software to nodes which does not have either master or tserver with new
          // version.
          createDownloadTasks(toOrderedSet(nodes.asPair()), oldVersion);

          if (ysqlMajorVersionUpgrade) {
            // Set ysql_yb_major_version_upgrade_compatibility to `11` for tservers during ysql
            // upgrade rollback.
            createGFlagsUpgradeTaskForYSQLMajorUpgrade(
                universe, YsqlMajorVersionUpgradeState.ROLLBACK_IN_PROGRESS);
          }

          if (nodes.tserversList.size() > 0) {
            createTServerUpgradeFlowTasks(
                universe,
                nodes.tserversList,
                oldVersion,
                getRollbackUpgradeContext(taskParams().ybSoftwareVersion),
                false /* reProvisionRequired */,
                ysqlMajorVersionUpgrade ? YsqlMajorVersionUpgradeState.ROLLBACK_IN_PROGRESS : null);
          }

          if (nodes.mastersList.size() > 0) {
            // Perform rollback ysql major version catalog upgrade only when all masters were
            // upgraded to the target ysql major version.
            if (ysqlMajorVersionUpgrade
                && prevYBSoftwareConfig != null
                && prevYBSoftwareConfig.isCanRollbackCatalogUpgrade()) {
              createRollbackYsqlMajorVersionCatalogUpgradeTask();
              createUpdateSoftwareUpdatePrevConfigTask(
                  false /* canRollbackCatalogUpgrade */,
                  false /* allTserversUpgradedToYsqlMajorVersion */);
            }

            createMasterUpgradeFlowTasks(
                universe,
                getNonMasterNodes(nodes.mastersList, nodes.tserversList),
                oldVersion,
                getRollbackUpgradeContext(taskParams().ybSoftwareVersion),
                ysqlMajorVersionUpgrade ? YsqlMajorVersionUpgradeState.ROLLBACK_IN_PROGRESS : null,
                false /* activeRole */);

            createMasterUpgradeFlowTasks(
                universe,
                nodes.mastersList,
                oldVersion,
                getRollbackUpgradeContext(taskParams().ybSoftwareVersion),
                ysqlMajorVersionUpgrade ? YsqlMajorVersionUpgradeState.ROLLBACK_IN_PROGRESS : null,
                true /* activeRole */);
          }

          if (ysqlMajorVersionUpgrade) {
            // Un-set the flag ysql_yb_major_version_upgrade_compatibility as major version upgrade
            // is
            // rolled back.
            createGFlagsUpgradeTaskForYSQLMajorUpgrade(
                universe, YsqlMajorVersionUpgradeState.ROLLBACK_COMPLETE);

            if (requireAdditionalSuperUserForCatalogUpgrade) {
              createManageCatalogUpgradeSuperUserTask(Action.DELETE_USER);
            }
          }

          // Re-enable PITR configs after successful rollback
          // This also updates intermittentMinRecoverTimeInMillis for all PITR configs
          createEnablePitrConfigTask();

          // Check software version on each node.
          createCheckSoftwareVersionTask(allNodes, oldVersion);

          // Update Software version
          createUpdateSoftwareVersionTask(oldVersion, false /*isSoftwareUpdateViaVm*/)
              .setSubTaskGroupType(getTaskSubGroupType());

          createUpdateUniverseSoftwareUpgradeStateTask(
              UniverseDefinitionTaskParams.SoftwareUpgradeState.Ready,
              false /* isSoftwareRollbackAllowed */);
        });
  }
}
