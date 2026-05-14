// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.tasks.subtasks.ManageCatalogUpgradeSuperUser.Action;
import com.yugabyte.yw.common.SoftwareUpgradeHelper;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.AutoFlagUtil;
import com.yugabyte.yw.forms.AZUpgradeState;
import com.yugabyte.yw.forms.AZUpgradeStatus;
import com.yugabyte.yw.forms.AZUpgradeStep;
import com.yugabyte.yw.forms.CanaryPauseState;
import com.yugabyte.yw.forms.CanaryUpgradeConfig;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.PrevYBSoftwareConfig;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeOption;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.UpgradeDetails.YsqlMajorVersionUpgradeState;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.annotation.Nullable;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
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
    if (isResumeTask()) {
      return;
    }
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

  /**
   * Immutable context for upgrade task creation. Used by both full run and canary resume so that a
   * single method (createUpgradeSubtasks) defines the upgrade sequence.
   */
  private static class UpgradeTaskCreationContext {
    final boolean isResume;
    final boolean mastersDone;
    final List<UUID> primaryAZsCompleted;
    final Map<UUID, List<UUID>> readReplicaAZsCompleted;
    final String newVersion;
    final String currentVersion;
    final boolean upgradeRequireFinalize;
    final boolean requireYsqlMajorVersionUpgrade;
    final boolean requireAdditionalSuperUserForCatalogUpgrade;
    final MastersAndTservers nodesToApply;
    final Set<NodeDetails> allNodes;

    UpgradeTaskCreationContext(
        boolean isResume,
        boolean mastersDone,
        List<UUID> primaryAZsCompleted,
        Map<UUID, List<UUID>> readReplicaAZsCompleted,
        String newVersion,
        String currentVersion,
        boolean upgradeRequireFinalize,
        boolean requireYsqlMajorVersionUpgrade,
        boolean requireAdditionalSuperUserForCatalogUpgrade,
        MastersAndTservers nodesToApply,
        Set<NodeDetails> allNodes) {
      this.isResume = isResume;
      this.mastersDone = mastersDone;
      this.primaryAZsCompleted =
          primaryAZsCompleted != null ? primaryAZsCompleted : Collections.emptyList();
      this.readReplicaAZsCompleted =
          readReplicaAZsCompleted != null ? readReplicaAZsCompleted : Collections.emptyMap();
      this.newVersion = newVersion;
      this.currentVersion = currentVersion;
      this.upgradeRequireFinalize = upgradeRequireFinalize;
      this.requireYsqlMajorVersionUpgrade = requireYsqlMajorVersionUpgrade;
      this.requireAdditionalSuperUserForCatalogUpgrade =
          requireAdditionalSuperUserForCatalogUpgrade;
      this.nodesToApply = nodesToApply;
      this.allNodes = allNodes;
    }
  }

  private UpgradeTaskCreationContext buildContext(Universe universe, boolean isResume) {
    String newVersion = taskParams().ybSoftwareVersion;
    String currentVersion =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
    boolean requireYsqlMajorVersionUpgrade =
        softwareUpgradeHelper.isYsqlMajorVersionUpgradeRequired(
            universe, currentVersion, newVersion);
    boolean requireAdditionalSuperUserForCatalogUpgrade =
        softwareUpgradeHelper.isSuperUserRequiredForCatalogUpgrade(
            universe, currentVersion, newVersion);
    boolean upgradeRequireFinalize =
        softwareUpgradeHelper.checkUpgradeRequireFinalize(currentVersion, newVersion);
    MastersAndTservers nodesToApply = getNodesToBeRestarted();
    Set<NodeDetails> allNodes = toOrderedSet(fetchNodes(taskParams().upgradeOption).asPair());

    boolean mastersDone = false;
    List<UUID> primaryAZsCompleted = Collections.emptyList();
    Map<UUID, List<UUID>> readReplicaAZsCompleted = Collections.emptyMap();
    if (isResume) {
      PrevYBSoftwareConfig prev = universe.getUniverseDetails().prevYBSoftwareConfig;
      mastersDone = deriveMastersDoneFromPrev(prev);
      primaryAZsCompleted = derivePrimaryCompletedTserverAZs(prev, universe);
      readReplicaAZsCompleted = deriveRrCompletedTserverAZs(prev, universe);
    }

    if (isResume) {
      log.info(
          "Canary resume context: mastersDone={}, primaryAZsCompleted={}, rrAZsCompleted={}",
          mastersDone,
          primaryAZsCompleted,
          readReplicaAZsCompleted);
    }

    return new UpgradeTaskCreationContext(
        isResume,
        mastersDone,
        primaryAZsCompleted,
        readReplicaAZsCompleted,
        newVersion,
        currentVersion,
        upgradeRequireFinalize,
        requireYsqlMajorVersionUpgrade,
        requireAdditionalSuperUserForCatalogUpgrade,
        nodesToApply,
        allNodes);
  }

  /**
   * Creates upgrade subtasks in phase order. Used by both full run and canary resume; ctx.isResume
   * and ctx.mastersDone / completed AZs determine which phases to run. Enqueues the full pipeline
   * (including POST_TSERVER); canary pause checkpoints are handled by {@code setPausedAfter} in
   * {@link com.yugabyte.yw.commissioner.TaskExecutor}.
   */
  private void createUpgradeSubtasks(Universe universe, UpgradeTaskCreationContext ctx) {
    MastersAndTservers nodesToApply = ctx.nodesToApply;

    if (!ctx.isResume) {
      nodesToApply = createPreFlightPhase(universe, ctx, nodesToApply);
    }

    createMastersPhase(universe, ctx, nodesToApply);

    createCatalogBeforeTserversPhase(universe, ctx, nodesToApply);

    if (nodesToApply.tserversList.size() > 0) {
      createTserverUpgradeTasksByAz(
          universe,
          nodesToApply.tserversList,
          ctx.newVersion,
          ctx.requireYsqlMajorVersionUpgrade,
          ctx.primaryAZsCompleted,
          ctx.readReplicaAZsCompleted,
          true);
    }

    createPostTserverPhase(universe, ctx, nodesToApply);
  }

  /**
   * PRE_FLIGHT: runs only on full (non-resume) run. Updates universe state, PITR/xcluster/autoflag,
   * optional YSQL catalog rollback, download tasks, and YSQL major upgrade prep. Returns the nodes
   * to apply for subsequent phases (may differ from ctx after a master rollback).
   */
  private MastersAndTservers createPreFlightPhase(
      Universe universe, UpgradeTaskCreationContext ctx, MastersAndTservers nodesToApply) {
    createUpdateUniverseSoftwareUpgradeStateTask(
        UniverseDefinitionTaskParams.SoftwareUpgradeState.Upgrading,
        true /* isSoftwareRollbackAllowed */);

    if (ctx.upgradeRequireFinalize) {
      createDisablePitrConfigTask();
    }

    if (!universe.getUniverseDetails().xClusterInfo.isSourceRootCertDirPathGflagConfigured()) {
      createXClusterSourceRootCertDirPathGFlagTasks();
    }

    createStoreAutoFlagConfigVersionTask(taskParams().getUniverseUUID(), ctx.newVersion);
    createSaveSoftwareUpgradeProgressTask(
        true /* isCanaryUpgrade */,
        CanaryPauseState.NOT_PAUSED,
        buildAZUpgradeStatesList(
            universe, ServerType.MASTER, universe.getMasters(), Collections.emptyMap()),
        buildAZUpgradeStatesList(
            universe, ServerType.TSERVER, universe.getTServers(), Collections.emptyMap()),
        false /* pauseAfter */);

    boolean rollbackMaster = false;
    if (ctx.requireAdditionalSuperUserForCatalogUpgrade) {
      if (softwareUpgradeHelper.isAllMasterUpgradedToYsqlMajorVersion(universe, "15")) {
        YsqlMajorCatalogUpgradeState catalogUpgradeState =
            softwareUpgradeHelper.getYsqlMajorCatalogUpgradeState(universe);
        if (catalogUpgradeState.equals(
            YsqlMajorCatalogUpgradeState.YSQL_MAJOR_CATALOG_UPGRADE_PENDING_ROLLBACK)) {
          log.info("YSQL catalog upgrade is in a failed state. Rolling back catalog upgrade.");
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
      createDownloadTasks(universe.getMasters(), ctx.currentVersion);
      upgradeMaster(
          universe,
          universe.getMasters(),
          ctx.currentVersion,
          YsqlMajorVersionUpgradeState.ROLLBACK_IN_PROGRESS,
          true,
          false);
      nodesToApply = new MastersAndTservers(universe.getMasters(), universe.getTServers());
    }

    createDownloadTasks(toOrderedSet(nodesToApply.asPair()), ctx.newVersion);

    if (ctx.requireYsqlMajorVersionUpgrade) {
      if (nodesToApply.mastersList.size() == universe.getMasters().size()) {
        createGFlagsUpgradeTaskForYSQLMajorUpgrade(
            universe, YsqlMajorVersionUpgradeState.IN_PROGRESS);
      }
      if (ctx.requireAdditionalSuperUserForCatalogUpgrade
          && nodesToApply.tserversList.size() == universe.getTServers().size()) {
        createManageCatalogUpgradeSuperUserTask(
            Action.CREATE_USER_AND_PG_PASS_FILE, Util.getPostgresCompatiblePassword());
      }
    }
    return nodesToApply;
  }

  /**
   * MASTERS: upgrades masters when not yet done. On resume, adds download and YSQL prep tasks
   * first.
   */
  private void createMastersPhase(
      Universe universe, UpgradeTaskCreationContext ctx, MastersAndTservers nodesToApply) {
    if (ctx.mastersDone || nodesToApply.mastersList.size() == 0) {
      return;
    }
    if (ctx.isResume) {
      createDownloadTasks(toOrderedSet(nodesToApply.asPair()), ctx.newVersion);
      if (ctx.requireYsqlMajorVersionUpgrade
          && nodesToApply.mastersList.size() == universe.getMasters().size()) {
        createGFlagsUpgradeTaskForYSQLMajorUpgrade(
            universe, YsqlMajorVersionUpgradeState.IN_PROGRESS);
        if (ctx.requireAdditionalSuperUserForCatalogUpgrade
            && nodesToApply.tserversList.size() == universe.getTServers().size()) {
          createManageCatalogUpgradeSuperUserTask(
              Action.CREATE_USER_AND_PG_PASS_FILE, Util.getPostgresCompatiblePassword());
        }
      }
    }
    upgradeMaster(
        universe,
        getNonMasterNodes(nodesToApply.mastersList, nodesToApply.tserversList),
        ctx.newVersion,
        ctx.requireYsqlMajorVersionUpgrade ? YsqlMajorVersionUpgradeState.IN_PROGRESS : null,
        false,
        true);
    upgradeMaster(
        universe,
        nodesToApply.mastersList,
        ctx.newVersion,
        ctx.requireYsqlMajorVersionUpgrade ? YsqlMajorVersionUpgradeState.IN_PROGRESS : null,
        true,
        true);
    if (ctx.requireYsqlMajorVersionUpgrade) {
      createUpdateSoftwareUpdatePrevConfigTask(true, false);
    }
    CanaryUpgradeConfig canary = taskParams().canaryUpgradeConfig;
    if (canary != null && canary.pauseAfterMasters) {
      Map<UUID, Set<UUID>> masterDone = azsByClusterFromNodes(universe.getMasters());
      createSaveSoftwareUpgradeProgressTask(
          true,
          CanaryPauseState.PAUSED_AFTER_MASTERS,
          buildAZUpgradeStatesList(universe, ServerType.MASTER, universe.getMasters(), masterDone),
          buildAZUpgradeStatesList(
              universe, ServerType.TSERVER, universe.getTServers(), Collections.emptyMap()),
          true);
    }
  }

  /**
   * CATALOG_BEFORE_TSERVERS: runs YSQL major catalog upgrade and optional superuser cleanup when
   * all tservers are in scope.
   */
  private void createCatalogBeforeTserversPhase(
      Universe universe, UpgradeTaskCreationContext ctx, MastersAndTservers nodesToApply) {
    if (!ctx.requireYsqlMajorVersionUpgrade
        || nodesToApply.tserversList.size() != universe.getTServers().size()) {
      return;
    }
    createRunYsqlMajorVersionCatalogUpgradeTask();
    if (ctx.requireAdditionalSuperUserForCatalogUpgrade) {
      createManageCatalogUpgradeSuperUserTask(Action.DELETE_PG_PASS_FILE);
    }
  }

  /**
   * POST_TSERVER: YSQL completion, YBC install, version check, auto-flag promote, update version,
   * and finalize/state update. YSQL major catalog upgrade runs only in
   * createCatalogBeforeTserversPhase (when all tservers are in scope); it must not run again on
   * resume when nodesToApply contains only remaining tservers.
   */
  private void createPostTserverPhase(
      Universe universe, UpgradeTaskCreationContext ctx, MastersAndTservers nodesToApply) {
    if (ctx.requireYsqlMajorVersionUpgrade) {
      createUpdateSoftwareUpdatePrevConfigTask(true, true);
    }
    if (ctx.requireYsqlMajorVersionUpgrade) {
      createGFlagsUpgradeTaskForYSQLMajorUpgrade(
          universe, YsqlMajorVersionUpgradeState.UPGRADE_COMPLETE);
    }
    if (taskParams().installYbc) {
      createYbcInstallTask(universe, new ArrayList<>(ctx.allNodes), ctx.newVersion);
    }
    createCheckSoftwareVersionTask(ctx.allNodes, ctx.newVersion);
    createPromoteAutoFlagTask(
        universe.getUniverseUUID(),
        true /* ignoreErrors */,
        AutoFlagUtil.LOCAL_VOLATILE_AUTO_FLAG_CLASS_NAME /* maxClass */);
    createUpdateSoftwareVersionTask(ctx.newVersion, false /* isSoftwareUpdateViaVm */)
        .setSubTaskGroupType(getTaskSubGroupType());
    if (!taskParams().rollbackSupport) {
      createFinalizeUpgradeTasks(
          taskParams().upgradeSystemCatalog,
          ctx.requireYsqlMajorVersionUpgrade,
          ctx.requireAdditionalSuperUserForCatalogUpgrade);
    } else {
      if (ctx.upgradeRequireFinalize) {
        createUpdateUniverseSoftwareUpgradeStateTask(
            UniverseDefinitionTaskParams.SoftwareUpgradeState.PreFinalize,
            true /* isSoftwareRollbackAllowed */);
      } else {
        createClearSoftwareUpgradeProgressTask();
        createUpdateUniverseSoftwareUpgradeStateTask(
            UniverseDefinitionTaskParams.SoftwareUpgradeState.Ready,
            true /* isSoftwareRollbackAllowed */);
      }
    }
  }

  @Override
  public void run() {
    Universe universe = getUniverse();
    if (taskParams().canaryUpgradeConfig != null) {
      if (isResumeTask()) {
        runCanaryResume(universe);
      } else {
        runCanaryUpgrade(universe);
      }
      return;
    }
    runStandardUpgrade(universe);
  }

  /**
   * Standard (non-canary) upgrade flow. Mirrors the original run() lambda from before canary
   * support; contains no canary-related code paths.
   */
  private void runStandardUpgrade(Universe universe) {
    String newVersion = taskParams().ybSoftwareVersion;
    String currentVersion =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
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

          // Check whether this upgrade requires a separate finalize step.
          boolean upgradeRequireFinalize =
              softwareUpgradeHelper.checkUpgradeRequireFinalize(currentVersion, newVersion);

          if (upgradeRequireFinalize) {
            // Disable PITR configs at the start of software upgrade
            createDisablePitrConfigTask();
          }

          if (!universe
              .getUniverseDetails()
              .xClusterInfo
              .isSourceRootCertDirPathGflagConfigured()) {
            createXClusterSourceRootCertDirPathGFlagTasks();
          }

          createStoreAutoFlagConfigVersionTask(taskParams().getUniverseUUID(), newVersion);
          createSaveSoftwareUpgradeProgressTask(
              false /* isCanaryUpgrade */,
              null /* canaryPauseState */,
              buildAZUpgradeStatesList(
                  universe, ServerType.MASTER, universe.getMasters(), Collections.emptyMap()),
              buildAZUpgradeStatesList(
                  universe, ServerType.TSERVER, universe.getTServers(), Collections.emptyMap()),
              false /* pauseAfter */);

          boolean rollbackMaster = false;
          if (requireAdditionalSuperUserForCatalogUpgrade) {
            if (softwareUpgradeHelper.isAllMasterUpgradedToYsqlMajorVersion(universe, "15")) {
              YsqlMajorCatalogUpgradeState catalogUpgradeState =
                  softwareUpgradeHelper.getYsqlMajorCatalogUpgradeState(universe);
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
            upgradeMaster(
                universe,
                universe.getMasters(),
                currentVersion,
                YsqlMajorVersionUpgradeState.ROLLBACK_IN_PROGRESS,
                true,
                false);
            nodesToApply = new MastersAndTservers(universe.getMasters(), universe.getTServers());
          }

          // Download software to the nodes selected for this run
          // (nodes not already on newVersion).
          createDownloadTasks(toOrderedSet(nodesToApply.asPair()), newVersion);

          if (requireYsqlMajorVersionUpgrade) {
            if (nodesToApply.mastersList.size() == universe.getMasters().size()) {
              // Only set IN_PROGRESS when all masters are included in this run,
              // so resume/partial runs do not flip the global state prematurely.
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
            upgradeMaster(
                universe,
                getNonMasterNodes(nodesToApply.mastersList, nodesToApply.tserversList),
                newVersion,
                requireYsqlMajorVersionUpgrade ? YsqlMajorVersionUpgradeState.IN_PROGRESS : null,
                false,
                true);
            upgradeMaster(
                universe,
                nodesToApply.mastersList,
                newVersion,
                requireYsqlMajorVersionUpgrade ? YsqlMajorVersionUpgradeState.IN_PROGRESS : null,
                true,
                true);
          }

          if (requireYsqlMajorVersionUpgrade) {
            createUpdateSoftwareUpdatePrevConfigTask(true, false);
          }

          if (nodesToApply.tserversList.size() == universe.getTServers().size()) {
            // Run YSQL major catalog upgrade and optional superuser cleanup when
            // all tservers are in scope.
            if (requireYsqlMajorVersionUpgrade) {
              createRunYsqlMajorVersionCatalogUpgradeTask();
              if (requireAdditionalSuperUserForCatalogUpgrade) {
                // Delete the pg_pass file after catalog upgrade.
                createManageCatalogUpgradeSuperUserTask(Action.DELETE_PG_PASS_FILE);
              }
            }
          }

          if (nodesToApply.tserversList.size() > 0) {
            upgradeTServer(
                universe,
                nodesToApply.tserversList,
                newVersion,
                requireYsqlMajorVersionUpgrade,
                true);
          }

          if (requireYsqlMajorVersionUpgrade) {
            createUpdateSoftwareUpdatePrevConfigTask(true, true);
          }
          if (requireYsqlMajorVersionUpgrade) {
            // Reset ysql_yb_major_version_upgrade_compatibility after upgrade completion.
            createGFlagsUpgradeTaskForYSQLMajorUpgrade(
                universe, YsqlMajorVersionUpgradeState.UPGRADE_COMPLETE);
          }

          if (taskParams().installYbc) {
            createYbcInstallTask(universe, new ArrayList<>(allNodes), newVersion);
          }

          createCheckSoftwareVersionTask(allNodes, newVersion);

          createPromoteAutoFlagTask(
              universe.getUniverseUUID(),
              true /* ignoreErrors */,
              AutoFlagUtil.LOCAL_VOLATILE_AUTO_FLAG_CLASS_NAME /* maxClass */);
          createUpdateSoftwareVersionTask(newVersion, false /* isSoftwareUpdateViaVm */)
              .setSubTaskGroupType(getTaskSubGroupType());

          if (!taskParams().rollbackSupport) {
            // When rollback is not supported, finalize within this task
            // instead of entering PreFinalize.
            createFinalizeUpgradeTasks(
                taskParams().upgradeSystemCatalog,
                requireYsqlMajorVersionUpgrade,
                requireAdditionalSuperUserForCatalogUpgrade);
          } else {
            if (upgradeRequireFinalize) {
              createUpdateUniverseSoftwareUpgradeStateTask(
                  UniverseDefinitionTaskParams.SoftwareUpgradeState.PreFinalize,
                  true /* isSoftwareRollbackAllowed */);
            } else {
              createClearSoftwareUpgradeProgressTask();
              createUpdateUniverseSoftwareUpgradeStateTask(
                  UniverseDefinitionTaskParams.SoftwareUpgradeState.Ready,
                  true /* isSoftwareRollbackAllowed */);
            }
          }
        },
        null /* firstRunTxnCallback */,
        () -> {
          markInProgressSoftwareUpgradeAzsAsFailedOnTaskFailure();
          if (requireAdditionalSuperUserForCatalogUpgrade) {
            createManageCatalogUpgradeSuperUserTask(Action.DELETE_PG_PASS_FILE);
          }
        });
  }

  /** Canary upgrade flow (first run, not resume). Uses phase methods and canary pause points. */
  private void runCanaryUpgrade(Universe universe) {
    UpgradeTaskCreationContext ctx = buildContext(universe, false);
    final boolean requireAdditionalSuperUserForCatalogUpgrade =
        ctx.requireAdditionalSuperUserForCatalogUpgrade;
    runUpgrade(
        () -> createUpgradeSubtasks(universe, ctx),
        null /* firstRunTxnCallback */,
        () -> {
          markInProgressSoftwareUpgradeAzsAsFailedOnTaskFailure();
          if (requireAdditionalSuperUserForCatalogUpgrade) {
            createManageCatalogUpgradeSuperUserTask(Action.DELETE_PG_PASS_FILE);
          }
        });
  }

  private void upgradeMaster(
      Universe universe,
      List<NodeDetails> masterNodes,
      String version,
      YsqlMajorVersionUpgradeState ysqlMajorVersionUpgradeState,
      boolean activeRole,
      boolean trackSoftwareUpgradeProgress) {
    long sleepTime =
        confGetter.getConfForScope(universe, UniverseConfKeys.upgradeMasterStagePauseDurationMs);
    boolean hasCanaryConfig = taskParams().canaryUpgradeConfig != null;
    boolean targetUpgrade =
        trackSoftwareUpgradeProgress && version.equals(taskParams().ybSoftwareVersion);
    boolean isCanary = taskParams().canaryUpgradeConfig != null;
    if (taskParams().upgradeOption == UpgradeOption.NON_ROLLING_UPGRADE
        || (sleepTime <= 0 && !hasCanaryConfig)
        || !activeRole) {
      if (targetUpgrade && !masterNodes.isEmpty()) {
        Map<UUID, Set<UUID>> pendingMasterAzs = azsByClusterFromNodes(masterNodes);
        createSaveSoftwareUpgradeProgressTask(
            isCanary,
            isCanary ? CanaryPauseState.NOT_PAUSED : null,
            buildAZUpgradeStatesList(
                universe,
                ServerType.MASTER,
                universe.getMasters(),
                Collections.emptyMap(),
                pendingMasterAzs,
                null),
            buildAZUpgradeStatesList(
                universe, ServerType.TSERVER, universe.getTServers(), Collections.emptyMap()),
            false);
      }
      createMasterUpgradeFlowTasks(
          universe,
          masterNodes,
          version,
          getUpgradeContext(version),
          ysqlMajorVersionUpgradeState,
          activeRole);
      if (targetUpgrade && !masterNodes.isEmpty()) {
        Map<UUID, Set<UUID>> masterDone = azsByClusterFromNodes(masterNodes);
        createSaveSoftwareUpgradeProgressTask(
            isCanary,
            isCanary ? CanaryPauseState.NOT_PAUSED : null,
            buildAZUpgradeStatesList(
                universe, ServerType.MASTER, universe.getMasters(), masterDone),
            buildAZUpgradeStatesList(
                universe, ServerType.TSERVER, universe.getTServers(), Collections.emptyMap()),
            false);
      }
    } else {
      List<String> upgradedZones = new ArrayList<>();
      Map<UUID, Set<UUID>> masterCompletedByCluster = new HashMap<>();
      for (Cluster cluster : universe.getUniverseDetails().clusters) {
        List<UUID> azs =
            taskParams().canaryUpgradeConfig != null
                ? getAZOrderForCluster(cluster, universe)
                : sortAZs(cluster, universe);
        for (UUID azUUID : azs) {
          List<NodeDetails> nodesInAZ = getNodesInAZ(masterNodes, azUUID);
          if (nodesInAZ.isEmpty()) {
            continue;
          }
          if (targetUpgrade) {
            createSaveSoftwareUpgradeProgressTask(
                isCanary,
                isCanary ? CanaryPauseState.NOT_PAUSED : null,
                buildAZUpgradeStatesList(
                    universe,
                    ServerType.MASTER,
                    universe.getMasters(),
                    masterCompletedByCluster,
                    singleClusterAzInProgress(cluster.uuid, azUUID),
                    null),
                buildAZUpgradeStatesList(
                    universe, ServerType.TSERVER, universe.getTServers(), Collections.emptyMap()),
                false);
          }
          createMasterUpgradeFlowTasks(
              universe,
              nodesInAZ,
              version,
              getUpgradeContext(version),
              ysqlMajorVersionUpgradeState,
              activeRole);
          AvailabilityZone zone = AvailabilityZone.getOrBadRequest(azUUID);
          upgradedZones.add(zone.getName());
          masterCompletedByCluster
              .computeIfAbsent(cluster.uuid, k -> new LinkedHashSet<>())
              .add(azUUID);
          if (targetUpgrade) {
            createSaveSoftwareUpgradeProgressTask(
                isCanary,
                isCanary ? CanaryPauseState.NOT_PAUSED : null,
                buildAZUpgradeStatesList(
                    universe, ServerType.MASTER, universe.getMasters(), masterCompletedByCluster),
                buildAZUpgradeStatesList(
                    universe, ServerType.TSERVER, universe.getTServers(), Collections.emptyMap()),
                false);
          }
          if (sleepTime > 0) {
            String sleepMessage =
                String.format(
                    "Matsers are upgraded in AZ %s, Sleeping after upgrade master in AZ %s",
                    String.join(",", upgradedZones), zone.getName());
            createWaitForDurationSubtask(
                universe.getUniverseUUID(), Duration.ofMillis(sleepTime), sleepMessage);
          }
        }
      }
    }
  }

  /**
   * Standard-flow tserver upgrade. Uses sortAZs for AZ order; no canary-specific logic. Called only
   * by runStandardUpgrade.
   */
  private void upgradeTServer(
      Universe universe,
      List<NodeDetails> tserverNodes,
      String version,
      boolean requireYsqlMajorVersionUpgrade,
      boolean trackSoftwareUpgradeProgress) {
    long sleepTime =
        confGetter.getConfForScope(universe, UniverseConfKeys.upgradeTServerStagePauseDurationMs);
    boolean targetUpgrade =
        trackSoftwareUpgradeProgress && version.equals(taskParams().ybSoftwareVersion);
    boolean isCanary = taskParams().canaryUpgradeConfig != null;
    if (taskParams().upgradeOption == UpgradeOption.NON_ROLLING_UPGRADE || sleepTime <= 0) {
      if (targetUpgrade && !tserverNodes.isEmpty()) {
        Map<UUID, Set<UUID>> pendingTserverAzs = azsByClusterFromNodes(tserverNodes);
        createSaveSoftwareUpgradeProgressTask(
            isCanary,
            null,
            buildAZUpgradeStatesList(
                universe,
                ServerType.MASTER,
                universe.getMasters(),
                azsByClusterFromNodes(universe.getMasters())),
            buildAZUpgradeStatesList(
                universe,
                ServerType.TSERVER,
                universe.getTServers(),
                Collections.emptyMap(),
                pendingTserverAzs,
                null),
            false);
      }
      createTServerUpgradeFlowTasks(
          universe,
          tserverNodes,
          version,
          getUpgradeContext(version),
          taskParams().installYbc
              && !Util.isOnPremManualProvisioning(universe)
              && universe.getUniverseDetails().getPrimaryCluster().userIntent.useSystemd,
          requireYsqlMajorVersionUpgrade ? YsqlMajorVersionUpgradeState.IN_PROGRESS : null);
      if (targetUpgrade && !tserverNodes.isEmpty()) {
        Map<UUID, Set<UUID>> tDone = azsByClusterFromNodes(tserverNodes);
        createSaveSoftwareUpgradeProgressTask(
            isCanary,
            null,
            buildAZUpgradeStatesList(
                universe,
                ServerType.MASTER,
                universe.getMasters(),
                azsByClusterFromNodes(universe.getMasters())),
            buildAZUpgradeStatesList(universe, ServerType.TSERVER, universe.getTServers(), tDone),
            false);
      }
    } else {
      List<String> upgradedZones = new ArrayList<>();
      Map<UUID, Set<UUID>> tserverCompletedByCluster = new HashMap<>();
      for (Cluster cluster : universe.getUniverseDetails().clusters) {
        List<UUID> azs = sortAZs(cluster, universe);
        List<NodeDetails> tserverNodesForCluster =
            tserverNodes.stream()
                .filter(n -> cluster.uuid.equals(n.placementUuid))
                .collect(Collectors.toList());
        for (UUID azUUID : azs) {
          List<NodeDetails> nodesInAZ = getNodesInAZ(tserverNodesForCluster, azUUID);
          if (nodesInAZ.isEmpty()) {
            continue;
          }
          if (targetUpgrade) {
            createSaveSoftwareUpgradeProgressTask(
                isCanary,
                null,
                buildAZUpgradeStatesList(
                    universe,
                    ServerType.MASTER,
                    universe.getMasters(),
                    azsByClusterFromNodes(universe.getMasters())),
                buildAZUpgradeStatesList(
                    universe,
                    ServerType.TSERVER,
                    universe.getTServers(),
                    tserverCompletedByCluster,
                    singleClusterAzInProgress(cluster.uuid, azUUID),
                    null),
                false);
          }
          createTServerUpgradeFlowTasks(
              universe,
              nodesInAZ,
              version,
              getUpgradeContext(version),
              taskParams().installYbc
                  && !Util.isOnPremManualProvisioning(universe)
                  && universe.getUniverseDetails().getPrimaryCluster().userIntent.useSystemd,
              requireYsqlMajorVersionUpgrade ? YsqlMajorVersionUpgradeState.IN_PROGRESS : null);
          AvailabilityZone zone = AvailabilityZone.getOrBadRequest(azUUID);
          upgradedZones.add(zone.getName());
          tserverCompletedByCluster
              .computeIfAbsent(cluster.uuid, k -> new LinkedHashSet<>())
              .add(azUUID);
          if (targetUpgrade) {
            createSaveSoftwareUpgradeProgressTask(
                isCanary,
                null,
                buildAZUpgradeStatesList(
                    universe,
                    ServerType.MASTER,
                    universe.getMasters(),
                    azsByClusterFromNodes(universe.getMasters())),
                buildAZUpgradeStatesList(
                    universe,
                    ServerType.TSERVER,
                    universe.getTServers(),
                    tserverCompletedByCluster),
                false);
          }
          String sleepMessage =
              String.format(
                  "Tservers are upgraded in AZ %s, Sleeping after upgrade tserver in AZ %s",
                  String.join(",", upgradedZones), zone.getName());
          createWaitForDurationSubtask(
              universe.getUniverseUUID(), Duration.ofMillis(sleepTime), sleepMessage);
        }
      }
    }
  }

  /**
   * Creates tserver upgrade tasks by cluster/AZ. Skips AZs in completedPrimaryAZs and
   * completedRrAZs (used on resume). When {@code injectCanaryPause} is true, may add
   * SaveSoftwareUpgradeProgress with {@code setPausedAfter} for configured AZs; the caller still
   * enqueues remaining AZs and POST_TSERVER. Preview tail child rows (after the last successful
   * subtask) are removed when resuming in {@link
   * com.yugabyte.yw.controllers.handlers.UpgradeUniverseHandler#resumeCanarySoftwareUpgrade}.
   *
   * <p>Persisted software-upgrade progress for the target release is written when {@code version}
   * equals {@code taskParams().ybSoftwareVersion} (not when using intermediate versions). {@code
   * injectCanaryPause} only affects canary pause-after-AZ configuration, not that version gate.
   */
  private void createTserverUpgradeTasksByAz(
      Universe universe,
      List<NodeDetails> tserverNodes,
      String version,
      boolean requireYsqlMajorVersionUpgrade,
      List<UUID> completedPrimaryAZs,
      Map<UUID, List<UUID>> completedRrAZs,
      boolean injectCanaryPause) {
    long sleepTime =
        confGetter.getConfForScope(universe, UniverseConfKeys.upgradeTServerStagePauseDurationMs);
    boolean hasCanaryAZSteps =
        injectCanaryPause
            && getCanaryStepsForCluster(universe.getUniverseDetails().getPrimaryCluster(), universe)
                != null;
    if (taskParams().upgradeOption == UpgradeOption.NON_ROLLING_UPGRADE
        || (sleepTime <= 0 && !hasCanaryAZSteps)) {
      if (version.equals(taskParams().ybSoftwareVersion)) {
        Map<UUID, Set<UUID>> pendingTserverAzs = azsByClusterFromNodes(tserverNodes);
        createSaveSoftwareUpgradeProgressTask(
            true,
            CanaryPauseState.NOT_PAUSED,
            buildAZUpgradeStatesList(
                universe,
                ServerType.MASTER,
                universe.getMasters(),
                azsByClusterFromNodes(universe.getMasters())),
            buildAZUpgradeStatesList(
                universe,
                ServerType.TSERVER,
                universe.getTServers(),
                Collections.emptyMap(),
                pendingTserverAzs,
                null),
            false);
      }
      createTServerUpgradeFlowTasks(
          universe,
          tserverNodes,
          version,
          getUpgradeContext(version),
          taskParams().installYbc
              && !Util.isOnPremManualProvisioning(universe)
              && universe.getUniverseDetails().getPrimaryCluster().userIntent.useSystemd,
          requireYsqlMajorVersionUpgrade ? YsqlMajorVersionUpgradeState.IN_PROGRESS : null);
      if (version.equals(taskParams().ybSoftwareVersion)) {
        Map<UUID, Set<UUID>> tDone = azsByClusterFromNodes(tserverNodes);
        createSaveSoftwareUpgradeProgressTask(
            true,
            CanaryPauseState.NOT_PAUSED,
            buildAZUpgradeStatesList(
                universe,
                ServerType.MASTER,
                universe.getMasters(),
                azsByClusterFromNodes(universe.getMasters())),
            buildAZUpgradeStatesList(universe, ServerType.TSERVER, universe.getTServers(), tDone),
            false);
      }
      return;
    }
    List<UUID> primaryAZsCompleted =
        new ArrayList<>(
            completedPrimaryAZs != null ? completedPrimaryAZs : Collections.emptyList());
    Map<UUID, List<UUID>> rrAZsCompleted = new HashMap<>();
    if (completedRrAZs != null) {
      completedRrAZs.forEach(
          (k, v) -> rrAZsCompleted.put(k, v != null ? new ArrayList<>(v) : new ArrayList<>()));
    }
    UUID primaryClusterUuid = universe.getUniverseDetails().getPrimaryCluster().uuid;
    List<String> upgradedZones = new ArrayList<>();
    for (UniverseDefinitionTaskParams.Cluster cluster : universe.getUniverseDetails().clusters) {
      List<UUID> azs = getAZOrderForCluster(cluster, universe);
      Set<UUID> pauseAfterAZs = buildPauseAfterAZSet(injectCanaryPause, cluster, universe);
      List<UUID> completedForCluster =
          cluster.uuid.equals(primaryClusterUuid)
              ? primaryAZsCompleted
              : rrAZsCompleted.getOrDefault(cluster.uuid, Collections.emptyList());
      log.debug(
          "Tserver AZ upgrade for cluster {}: azOrder={}, pauseAfterAZs={}, completed={}",
          cluster.uuid,
          azs,
          pauseAfterAZs,
          completedForCluster);
      List<NodeDetails> tserverNodesForCluster =
          tserverNodes.stream()
              .filter(n -> cluster.uuid.equals(n.placementUuid))
              .collect(Collectors.toList());
      for (UUID azUUID : azs) {
        if (completedForCluster.contains(azUUID)) {
          continue;
        }
        List<NodeDetails> nodesInAZ = getNodesInAZ(tserverNodesForCluster, azUUID);
        if (nodesInAZ.isEmpty()) {
          continue;
        }
        Map<UUID, Set<UUID>> tserverDoneBeforeThisAz =
            tserverProgressMapFromCompletedLists(
                primaryClusterUuid, primaryAZsCompleted, rrAZsCompleted);
        if (version.equals(taskParams().ybSoftwareVersion)) {
          createSaveSoftwareUpgradeProgressTask(
              true,
              CanaryPauseState.NOT_PAUSED,
              buildAZUpgradeStatesList(
                  universe,
                  ServerType.MASTER,
                  universe.getMasters(),
                  azsByClusterFromNodes(universe.getMasters())),
              buildAZUpgradeStatesList(
                  universe,
                  ServerType.TSERVER,
                  universe.getTServers(),
                  tserverDoneBeforeThisAz,
                  singleClusterAzInProgress(cluster.uuid, azUUID),
                  null),
              false);
        }
        createTServerUpgradeFlowTasks(
            universe,
            nodesInAZ,
            version,
            getUpgradeContext(version),
            taskParams().installYbc
                && !Util.isOnPremManualProvisioning(universe)
                && universe.getUniverseDetails().getPrimaryCluster().userIntent.useSystemd,
            requireYsqlMajorVersionUpgrade ? YsqlMajorVersionUpgradeState.IN_PROGRESS : null);
        AvailabilityZone zone = AvailabilityZone.getOrBadRequest(azUUID);
        upgradedZones.add(zone.getName());
        if (cluster.uuid.equals(primaryClusterUuid)) {
          primaryAZsCompleted.add(azUUID);
        } else {
          rrAZsCompleted.computeIfAbsent(cluster.uuid, k -> new ArrayList<>()).add(azUUID);
        }
        Map<UUID, Set<UUID>> tserverDoneSoFar =
            tserverProgressMapFromCompletedLists(
                primaryClusterUuid, primaryAZsCompleted, rrAZsCompleted);
        if (pauseAfterAZs != null && pauseAfterAZs.contains(azUUID)) {
          log.info("Canary pause after tserver AZ {} ({})", zone.getName(), azUUID);
          createSaveSoftwareUpgradeProgressTask(
              true,
              CanaryPauseState.PAUSED_AFTER_TSERVERS_AZ,
              buildAZUpgradeStatesList(
                  universe,
                  ServerType.MASTER,
                  universe.getMasters(),
                  azsByClusterFromNodes(universe.getMasters())),
              buildAZUpgradeStatesList(
                  universe, ServerType.TSERVER, universe.getTServers(), tserverDoneSoFar),
              true);
          continue;
        }
        createSaveSoftwareUpgradeProgressTask(
            true,
            CanaryPauseState.NOT_PAUSED,
            buildAZUpgradeStatesList(
                universe,
                ServerType.MASTER,
                universe.getMasters(),
                azsByClusterFromNodes(universe.getMasters())),
            buildAZUpgradeStatesList(
                universe, ServerType.TSERVER, universe.getTServers(), tserverDoneSoFar),
            false);
        if (sleepTime > 0) {
          String sleepMessage =
              String.format(
                  "Tservers are upgraded in AZ %s, Sleeping after upgrade tserver in AZ %s",
                  String.join(",", upgradedZones), zone.getName());
          createWaitForDurationSubtask(
              universe.getUniverseUUID(), Duration.ofMillis(sleepTime), sleepMessage);
        }
      }
    }
  }

  private List<NodeDetails> getNodesInAZ(List<NodeDetails> nodes, UUID az) {
    return nodes.stream().filter(node -> node.azUuid.equals(az)).collect(Collectors.toList());
  }

  /**
   * Returns AZ UUIDs in upgrade order for the cluster. Uses canary config order when steps are
   * provided; when canary is set but primaryClusterAZSteps/readReplicaClusterAZSteps are null
   * (partial config), falls back to default sortAZs order by design (see SoftwareUpgradeParams
   * validation and CanaryUpgradeConfig API).
   */
  private List<UUID> getAZOrderForCluster(
      UniverseDefinitionTaskParams.Cluster cluster, Universe universe) {
    CanaryUpgradeConfig canary = taskParams().canaryUpgradeConfig;
    UUID primaryUuid = universe.getUniverseDetails().getPrimaryCluster().uuid;
    if (canary != null
        && cluster.uuid.equals(primaryUuid)
        && canary.primaryClusterAZSteps != null) {
      return canary.primaryClusterAZSteps.stream().map(s -> s.azUUID).collect(Collectors.toList());
    }
    if (canary != null
        && !cluster.uuid.equals(primaryUuid)
        && canary.readReplicaClusterAZSteps != null) {
      return canary.readReplicaClusterAZSteps.stream()
          .map(s -> s.azUUID)
          .collect(Collectors.toList());
    }
    return sortAZs(cluster, universe);
  }

  /**
   * Builds a set of AZ UUIDs that should trigger a canary pause after tserver upgrade. Returns null
   * when canary pause is disabled or no steps are configured for the cluster.
   */
  private Set<UUID> buildPauseAfterAZSet(
      boolean injectCanaryPause, UniverseDefinitionTaskParams.Cluster cluster, Universe universe) {
    if (!injectCanaryPause) {
      return null;
    }
    List<AZUpgradeStep> steps = getCanaryStepsForCluster(cluster, universe);
    if (steps == null) {
      return null;
    }
    return steps.stream()
        .filter(s -> s.pauseAfterTserverUpgrade)
        .map(s -> s.azUUID)
        .collect(Collectors.toSet());
  }

  /** Returns canary AZ steps for the cluster, or null if not using canary order. */
  private List<AZUpgradeStep> getCanaryStepsForCluster(
      UniverseDefinitionTaskParams.Cluster cluster, Universe universe) {
    CanaryUpgradeConfig canary = taskParams().canaryUpgradeConfig;
    if (canary == null) {
      return null;
    }
    UUID primaryUuid = universe.getUniverseDetails().getPrimaryCluster().uuid;
    if (cluster.uuid.equals(primaryUuid)) {
      return canary.primaryClusterAZSteps;
    }
    return canary.readReplicaClusterAZSteps;
  }

  private static boolean deriveMastersDoneFromPrev(PrevYBSoftwareConfig prev) {
    if (prev == null) {
      return false;
    }
    CanaryPauseState pause = prev.getCanaryPauseState();
    if (pause == CanaryPauseState.PAUSED_AFTER_MASTERS
        || pause == CanaryPauseState.PAUSED_AFTER_TSERVERS_AZ) {
      return true;
    }
    List<AZUpgradeState> masters = prev.getMasterAZUpgradeStatesList();
    if (masters == null || masters.isEmpty()) {
      return false;
    }
    return masters.stream().allMatch(s -> s.getStatus() == AZUpgradeStatus.COMPLETED);
  }

  private static List<UUID> derivePrimaryCompletedTserverAZs(
      PrevYBSoftwareConfig prev, Universe universe) {
    if (prev == null || prev.getTserverAZUpgradeStatesList() == null) {
      return Collections.emptyList();
    }
    UUID primaryUuid = universe.getUniverseDetails().getPrimaryCluster().uuid;
    List<UUID> ordered = new ArrayList<>();
    for (AZUpgradeState s : prev.getTserverAZUpgradeStatesList()) {
      if (primaryUuid.equals(s.getClusterUUID())
          && s.getStatus() == AZUpgradeStatus.COMPLETED
          && !ordered.contains(s.getAzUUID())) {
        ordered.add(s.getAzUUID());
      }
    }
    return ordered;
  }

  private static Map<UUID, List<UUID>> deriveRrCompletedTserverAZs(
      PrevYBSoftwareConfig prev, Universe universe) {
    Map<UUID, List<UUID>> map = new HashMap<>();
    if (prev == null || prev.getTserverAZUpgradeStatesList() == null) {
      return map;
    }
    UUID primaryUuid = universe.getUniverseDetails().getPrimaryCluster().uuid;
    for (AZUpgradeState s : prev.getTserverAZUpgradeStatesList()) {
      if (!primaryUuid.equals(s.getClusterUUID()) && s.getStatus() == AZUpgradeStatus.COMPLETED) {
        map.computeIfAbsent(s.getClusterUUID(), k -> new ArrayList<>());
        List<UUID> lst = map.get(s.getClusterUUID());
        if (!lst.contains(s.getAzUUID())) {
          lst.add(s.getAzUUID());
        }
      }
    }
    return map;
  }

  /**
   * For each cluster (placement), collects distinct AZ UUIDs where the given nodes are placed. Used
   * when building progress snapshots: as {@code completedByCluster} after those AZs finish, or as
   * {@code inProgressByCluster} when marking all touched AZs in-flight before a bulk upgrade.
   */
  private static Map<UUID, Set<UUID>> azsByClusterFromNodes(List<NodeDetails> nodes) {
    Map<UUID, Set<UUID>> m = new HashMap<>();
    if (nodes == null) {
      return m;
    }
    for (NodeDetails n : nodes) {
      if (n.placementUuid != null && n.azUuid != null) {
        m.computeIfAbsent(n.placementUuid, k -> new LinkedHashSet<>()).add(n.azUuid);
      }
    }
    return m;
  }

  private static Map<UUID, Set<UUID>> tserverProgressMapFromCompletedLists(
      UUID primaryClusterUuid,
      List<UUID> primaryAZsCompleted,
      Map<UUID, List<UUID>> rrAZsCompleted) {
    Map<UUID, Set<UUID>> m = new HashMap<>();
    for (UUID az : primaryAZsCompleted) {
      m.computeIfAbsent(primaryClusterUuid, k -> new LinkedHashSet<>()).add(az);
    }
    if (rrAZsCompleted != null) {
      rrAZsCompleted.forEach(
          (cl, azs) -> {
            if (azs != null) {
              for (UUID az : azs) {
                m.computeIfAbsent(cl, k -> new LinkedHashSet<>()).add(az);
              }
            }
          });
    }
    return m;
  }

  private List<AZUpgradeState> buildAZUpgradeStatesList(
      Universe universe,
      ServerType serverType,
      List<NodeDetails> roleNodes,
      Map<UUID, Set<UUID>> completedByCluster) {
    return buildAZUpgradeStatesList(
        universe,
        serverType,
        roleNodes,
        completedByCluster,
        null /* inProgressByCluster */,
        null /* failedByCluster */);
  }

  /**
   * Builds per-AZ upgrade state. Precedence per AZ: {@link AZUpgradeStatus#COMPLETED} if in {@code
   * completedByCluster}; else {@link AZUpgradeStatus#FAILED} if in {@code failedByCluster}; else
   * {@link AZUpgradeStatus#IN_PROGRESS} if in {@code inProgressByCluster}; else {@link
   * AZUpgradeStatus#NOT_STARTED}.
   */
  private List<AZUpgradeState> buildAZUpgradeStatesList(
      Universe universe,
      ServerType serverType,
      List<NodeDetails> roleNodes,
      Map<UUID, Set<UUID>> completedByCluster,
      @Nullable Map<UUID, Set<UUID>> inProgressByCluster,
      @Nullable Map<UUID, Set<UUID>> failedByCluster) {
    List<AZUpgradeState> out = new ArrayList<>();
    Map<UUID, Set<UUID>> inProg =
        inProgressByCluster != null ? inProgressByCluster : Collections.emptyMap();
    Map<UUID, Set<UUID>> failed =
        failedByCluster != null ? failedByCluster : Collections.emptyMap();
    for (Cluster cluster : universe.getUniverseDetails().clusters) {
      List<UUID> azOrder = azOrderForProgress(universe, cluster, serverType);
      Set<UUID> done = completedByCluster.getOrDefault(cluster.uuid, Collections.emptySet());
      Set<UUID> inProgress = inProg.getOrDefault(cluster.uuid, Collections.emptySet());
      Set<UUID> failedAzs = failed.getOrDefault(cluster.uuid, Collections.emptySet());
      // Restrict role nodes to this cluster so AZs shared between primary and read-replica
      // do not leak entries (e.g., MASTER entries for read-replica clusters).
      List<NodeDetails> roleNodesInCluster =
          roleNodes.stream()
              .filter(n -> cluster.uuid.equals(n.placementUuid))
              .collect(Collectors.toList());
      for (UUID azUUID : azOrder) {
        if (getNodesInAZ(roleNodesInCluster, azUUID).isEmpty()) {
          continue;
        }
        AvailabilityZone zone = AvailabilityZone.getOrBadRequest(azUUID);
        AZUpgradeStatus st;
        if (done.contains(azUUID)) {
          st = AZUpgradeStatus.COMPLETED;
        } else if (failedAzs.contains(azUUID)) {
          st = AZUpgradeStatus.FAILED;
        } else if (inProgress.contains(azUUID)) {
          st = AZUpgradeStatus.IN_PROGRESS;
        } else {
          st = AZUpgradeStatus.NOT_STARTED;
        }
        out.add(new AZUpgradeState(azUUID, zone.getName(), serverType, cluster.uuid, st));
      }
    }
    return out;
  }

  private static Map<UUID, Set<UUID>> singleClusterAzInProgress(UUID clusterUuid, UUID azUuid) {
    Map<UUID, Set<UUID>> m = new HashMap<>();
    m.put(clusterUuid, new LinkedHashSet<>(Collections.singleton(azUuid)));
    return m;
  }

  private List<UUID> azOrderForProgress(Universe universe, Cluster cluster, ServerType serverType) {
    if (taskParams().canaryUpgradeConfig != null) {
      return getAZOrderForCluster(cluster, universe);
    }
    return sortAZs(cluster, universe);
  }

  private boolean isResumeTask() {
    Universe universe = getUniverse();
    if (universe.getUniverseDetails().softwareUpgradeState
        != UniverseDefinitionTaskParams.SoftwareUpgradeState.Paused) {
      return false;
    }
    UniverseDefinitionTaskParams d = universe.getUniverseDetails();
    if (!getUserTaskUUID().equals(d.placementModificationTaskUuid)) {
      return false;
    }
    List<TaskInfo> subtasks = TaskInfo.getOrBadRequest(getUserTaskUUID()).getSubTasks();
    return CollectionUtils.isNotEmpty(subtasks);
  }

  /** Creates only the remaining subtask groups for a canary resume. */
  private void runCanaryResume(Universe universe) {
    UpgradeTaskCreationContext ctx = buildContext(universe, true);
    final boolean requireAdditionalSuperUserForCatalogUpgrade =
        ctx.requireAdditionalSuperUserForCatalogUpgrade;
    runUpgrade(
        () -> createUpgradeSubtasks(universe, ctx),
        null,
        () -> {
          markInProgressSoftwareUpgradeAzsAsFailedOnTaskFailure();
          if (requireAdditionalSuperUserForCatalogUpgrade) {
            createManageCatalogUpgradeSuperUserTask(Action.DELETE_PG_PASS_FILE);
          }
        });
  }

  /**
   * When the upgrade task fails mid-flight, any AZ left {@link AZUpgradeStatus#IN_PROGRESS} in
   * persisted progress should become {@link AZUpgradeStatus#FAILED}.
   */
  private void markInProgressSoftwareUpgradeAzsAsFailedOnTaskFailure() {
    try {
      saveUniverseDetails(
          universe -> {
            UniverseDefinitionTaskParams details = universe.getUniverseDetails();
            if (details.prevYBSoftwareConfig != null) {
              details.prevYBSoftwareConfig.markInProgressAzUpgradeStatusesAsFailed();
            }
            universe.setUniverseDetails(details);
          });
    } catch (Exception e) {
      log.warn(
          "Could not mark IN_PROGRESS software upgrade AZ progress as FAILED after task error: {}",
          e.getMessage());
    }
  }
}
