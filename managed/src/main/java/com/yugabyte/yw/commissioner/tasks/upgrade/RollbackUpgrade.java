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
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.stream.Collectors;
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
          }

          // Download target software on all nodes that need restart (masters + tservers in one
          // concurrent SubTaskGroup; non-live nodes are a subset of nodes).
          createDownloadTasks(toOrderedSet(nodes.asPair()), oldVersion);

          MastersAndTservers nonLive = getNonLiveServers(universe);
          if (prevYBSoftwareConfig != null) {
            createCompleteRollbackForNonLiveServersTasks(
                universe, oldVersion, ysqlMajorVersionUpgrade, nonLive);

            // RollbackAutoFlags is a cluster-wide master RPC; skip when all masters are already on
            // the target version (e.g. rolled back in a prior attempt) so retries do not re-emit
            // it.
            if (nodes.mastersList.size() > 0) {
              int autoFlagConfigVersion = prevYBSoftwareConfig.getAutoFlagConfigVersion();
              // Restore old auto flag Config
              createRollbackAutoFlagTask(taskParams().getUniverseUUID(), autoFlagConfigVersion);
            }
          }

          // Exclude non-live nodes already rolled back above from the main rolling-restart flows.
          MastersAndTservers nodesToRoll = excludeNodes(nodes, nonLive);

          if (ysqlMajorVersionUpgrade) {
            // Set ysql_yb_major_version_upgrade_compatibility to `11` for tservers during ysql
            // upgrade rollback.
            createGFlagsUpgradeTaskForYSQLMajorUpgrade(
                universe, YsqlMajorVersionUpgradeState.ROLLBACK_IN_PROGRESS);
          }

          if (nodesToRoll.tserversList.size() > 0) {
            createTServerUpgradeFlowTasks(
                universe,
                nodesToRoll.tserversList,
                oldVersion,
                getRollbackUpgradeContext(taskParams().ybSoftwareVersion),
                false /* reProvisionRequired */,
                ysqlMajorVersionUpgrade ? YsqlMajorVersionUpgradeState.ROLLBACK_IN_PROGRESS : null);
          }

          // Gate on the (pre-dedup) restart set: when masters are already on the target version
          // (e.g. rolled back in a prior attempt) they are filtered out and nodes.mastersList is
          // empty, so the catalog rollback RPC (GetYsqlMajorCatalogUpgradeState, which only exists
          // on the new-version master) is not issued against an already-rolled-back master.
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

            if (nodesToRoll.mastersList.size() > 0) {
              createMasterUpgradeFlowTasks(
                  universe,
                  getNonMasterNodes(nodesToRoll.mastersList, nodesToRoll.tserversList),
                  oldVersion,
                  getRollbackUpgradeContext(taskParams().ybSoftwareVersion),
                  ysqlMajorVersionUpgrade
                      ? YsqlMajorVersionUpgradeState.ROLLBACK_IN_PROGRESS
                      : null,
                  false /* activeRole */);

              createMasterUpgradeFlowTasks(
                  universe,
                  nodesToRoll.mastersList,
                  oldVersion,
                  getRollbackUpgradeContext(taskParams().ybSoftwareVersion),
                  ysqlMajorVersionUpgrade
                      ? YsqlMajorVersionUpgradeState.ROLLBACK_IN_PROGRESS
                      : null,
                  true /* activeRole */);
            }
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

  /** Returns masters and tservers that are not in Live state. */
  private MastersAndTservers getNonLiveServers(Universe universe) {
    List<NodeDetails> nonLiveMasters =
        universe.getMasters().stream()
            .filter(n -> n.state != NodeState.Live)
            .collect(Collectors.toList());
    List<NodeDetails> nonLiveTservers =
        universe.getTServers().stream()
            .filter(n -> n.state != NodeState.Live)
            .collect(Collectors.toList());
    return new MastersAndTservers(nonLiveMasters, nonLiveTservers);
  }

  /**
   * Completely rolls back non-live masters and tservers (e.g. left stopped by a prior aborted
   * rollback) to the target version so subsequent in-memory RPCs (SetFlagInMemory,
   * RollbackAutoFlags) can reach them. No-op when {@code nonLive} is empty. Non-live tservers are
   * rolled back before non-live masters so an old-version master is not brought up while live
   * tservers are still on the new version.
   */
  private void createCompleteRollbackForNonLiveServersTasks(
      Universe universe,
      String oldVersion,
      boolean ysqlMajorVersionUpgrade,
      MastersAndTservers nonLive) {
    YsqlMajorVersionUpgradeState state =
        ysqlMajorVersionUpgrade ? YsqlMajorVersionUpgradeState.ROLLBACK_IN_PROGRESS : null;
    if (!nonLive.tserversList.isEmpty()) {
      log.info(
          "Completely rolling back non-live tservers before rollback RPCs: {}",
          nonLive.tserversList);
      createTServerUpgradeFlowTasks(
          universe,
          nonLive.tserversList,
          oldVersion,
          getRollbackUpgradeContext(taskParams().ybSoftwareVersion),
          false /* reProvision */,
          state);
    }
    if (!nonLive.mastersList.isEmpty()) {
      log.info(
          "Completely rolling back non-live masters before rollback RPCs: {}", nonLive.mastersList);
      createMasterUpgradeFlowTasks(
          universe,
          nonLive.mastersList,
          oldVersion,
          getRollbackUpgradeContext(taskParams().ybSoftwareVersion),
          state,
          true /* activeRole */);
    }
  }

  /** Returns nodes from {@code nodes} that are not present in {@code toExclude}. */
  private static MastersAndTservers excludeNodes(
      MastersAndTservers nodes, MastersAndTservers toExclude) {
    Set<NodeDetails> excludeMasters = new HashSet<>(toExclude.mastersList);
    Set<NodeDetails> excludeTservers = new HashSet<>(toExclude.tserversList);
    List<NodeDetails> masters =
        nodes.mastersList.stream()
            .filter(n -> !excludeMasters.contains(n))
            .collect(Collectors.toList());
    List<NodeDetails> tservers =
        nodes.tserversList.stream()
            .filter(n -> !excludeTservers.contains(n))
            .collect(Collectors.toList());
    return new MastersAndTservers(masters, tservers);
  }
}
