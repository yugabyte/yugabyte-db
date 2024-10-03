// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.forms.RollbackUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.Set;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Abortable
@Retryable
public class RollbackUpgrade extends SoftwareUpgradeTaskBase {

  @Inject
  protected RollbackUpgrade(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
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
          MastersAndTservers nodes = getNodesToBeRestarted();
          Set<NodeDetails> allNodes = toOrderedSet(fetchNodes(taskParams().upgradeOption).asPair());
          Universe universe = getUniverse();

          UniverseDefinitionTaskParams.PrevYBSoftwareConfig prevYBSoftwareConfig =
              universe.getUniverseDetails().prevYBSoftwareConfig;
          String newVersion =
              universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;

          createUpdateUniverseSoftwareUpgradeStateTask(
              UniverseDefinitionTaskParams.SoftwareUpgradeState.RollingBack);

          // Skip auto flags restore in case upgrade did not take place or succeed.
          if (prevYBSoftwareConfig != null
              && !newVersion.equals(prevYBSoftwareConfig.getSoftwareVersion())) {
            newVersion = prevYBSoftwareConfig.getSoftwareVersion();
            int autoFlagConfigVersion = prevYBSoftwareConfig.getAutoFlagConfigVersion();
            // Restore old auto flag Config
            createRollbackAutoFlagTask(taskParams().getUniverseUUID(), autoFlagConfigVersion);
          }

          // Download software to nodes which does not have either master or tserver with new
          // version.
          createDownloadTasks(toOrderedSet(nodes.asPair()), newVersion);

          boolean ysqlMajorVersionUpgrade = false;
          if (prevYBSoftwareConfig != null) {
            ysqlMajorVersionUpgrade =
                gFlagsValidation.ysqlMajorVersionUpgrade(
                    prevYBSoftwareConfig.getSoftwareVersion(),
                    universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion);
          }

          if (nodes.tserversList.size() > 0) {
            createTServerUpgradeFlowTasks(
                universe,
                nodes.tserversList,
                newVersion,
                getRollbackUpgradeContext(taskParams().ybSoftwareVersion),
                false /* reProvisionRequired */);
          }

          if (nodes.mastersList.size() > 0) {
            // Perform rollback ysql major version catalog upgrade only when
            // none of the masters are upgraded.
            if (ysqlMajorVersionUpgrade
                && nodes.mastersList.size() == universe.getMasters().size()) {
              createRollbackYsqlMajorVersionCatalogUpgradeTask();
            }

            createMasterUpgradeFlowTasks(
                universe,
                getNonMasterNodes(nodes.mastersList, nodes.tserversList),
                newVersion,
                getRollbackUpgradeContext(taskParams().ybSoftwareVersion),
                false /* activeRole */);

            createMasterUpgradeFlowTasks(
                universe,
                nodes.mastersList,
                newVersion,
                getRollbackUpgradeContext(taskParams().ybSoftwareVersion),
                true /* activeRole */);
          }

          // Check software version on each node.
          createCheckSoftwareVersionTask(allNodes, newVersion);

          // Update Software version
          createUpdateSoftwareVersionTask(newVersion, false /*isSoftwareUpdateViaVm*/)
              .setSubTaskGroupType(getTaskSubGroupType());

          createUpdateUniverseSoftwareUpgradeStateTask(
              UniverseDefinitionTaskParams.SoftwareUpgradeState.Ready,
              false /* isSoftwareRollbackAllowed */);
        });
  }
}
