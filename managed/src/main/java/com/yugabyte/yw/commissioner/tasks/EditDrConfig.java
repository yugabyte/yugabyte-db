// Copyright (c) YugabyteDB, Inc.
package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.common.DrConfigStates.SourceUniverseState;
import com.yugabyte.yw.common.DrConfigStates.State;
import com.yugabyte.yw.common.DrConfigStates.TargetUniverseState;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.operator.OperatorStatusUpdater;
import com.yugabyte.yw.common.operator.OperatorStatusUpdaterFactory;
import com.yugabyte.yw.forms.DrConfigTaskParams;
import com.yugabyte.yw.models.DrConfig;
import com.yugabyte.yw.models.Restore;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import com.yugabyte.yw.models.XClusterTableConfig;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import javax.annotation.Nullable;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Abortable
@Retryable
public class EditDrConfig extends CreateXClusterConfig {

  private final OperatorStatusUpdater kubernetesStatus;

  @Inject
  protected EditDrConfig(
      BaseTaskDependencies baseTaskDependencies,
      XClusterUniverseService xClusterUniverseService,
      OperatorStatusUpdaterFactory operatorStatusUpdaterFactory) {
    super(baseTaskDependencies, xClusterUniverseService, operatorStatusUpdaterFactory);
    this.kubernetesStatus = operatorStatusUpdaterFactory.create();
  }

  @Override
  protected DrConfigTaskParams taskParams() {
    return (DrConfigTaskParams) taskParams;
  }

  @Override
  public String getName() {
    return String.format(
        "%s(uuid=%s,universe=%s)",
        this.getClass().getSimpleName(),
        taskParams().getDrConfig().getUuid(),
        taskParams().getUniverseUUID());
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    DrConfig drConfig = getDrConfigFromTaskParams();
    XClusterConfig currentXClusterConfig = taskParams().getOldXClusterConfig();
    // Either the task is created which means the old xCluster config exists or the task is retry-ed
    //  which means the old xCluster config can potentially be deleted.
    if (isFirstTry() && Objects.isNull(currentXClusterConfig)) {
      throw new IllegalStateException(
          "The old xCluster config does not exist and cannot do a switchover");
    } else if (!isFirstTry() && Objects.isNull(currentXClusterConfig)) {
      log.warn("The old xCluster config got deleted in the previous run");
    }
    XClusterConfig newXClusterConfig = getXClusterConfigFromTaskParams();

    Universe sourceUniverse = Universe.getOrBadRequest(newXClusterConfig.getSourceUniverseUUID());
    Universe targetUniverse = null;
    if (Objects.nonNull(currentXClusterConfig)) {
      targetUniverse = Universe.getOrBadRequest(currentXClusterConfig.getTargetUniverseUUID());
    }
    Universe newTargetUniverse =
        Universe.getOrBadRequest(newXClusterConfig.getTargetUniverseUUID());
    boolean taskSucceeded = false;
    try {
      // Lock the source universe.
      lockAndFreezeUniverseForUpdate(
          sourceUniverse.getUniverseUUID(), sourceUniverse.getVersion(), null /* Txn callback */);
      try {
        if (Objects.nonNull(targetUniverse)) {
          // Lock the target universe.
          lockAndFreezeUniverseForUpdate(
              targetUniverse.getUniverseUUID(),
              targetUniverse.getVersion(),
              null /* Txn callback */);
        }
        try {
          // Lock the new target universe.
          lockAndFreezeUniverseForUpdate(
              newTargetUniverse.getUniverseUUID(),
              newTargetUniverse.getVersion(),
              null /* Txn callback */);

          addSubtasksToUseNewXClusterConfig(
              currentXClusterConfig,
              newXClusterConfig,
              false /* forceDeleteCurrentXClusterConfig */);

          createMarkUniverseUpdateSuccessTasks(newTargetUniverse.getUniverseUUID())
              .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

          if (Objects.nonNull(targetUniverse)) {
            createMarkUniverseUpdateSuccessTasks(targetUniverse.getUniverseUUID())
                .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);
          }

          createMarkUniverseUpdateSuccessTasks(sourceUniverse.getUniverseUUID())
              .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

          getRunnableTask().runSubTasks();
        } finally {
          // Unlock the new target universe.
          unlockUniverseForUpdate(newTargetUniverse.getUniverseUUID());
        }
      } finally {
        if (Objects.nonNull(targetUniverse)) {
          // Unlock the target universe.
          unlockUniverseForUpdate(targetUniverse.getUniverseUUID());
        }
      }
      taskSucceeded = true;
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      newXClusterConfig.refresh();
      // Set xCluster config status to failed.
      newXClusterConfig.updateStatus(XClusterConfigStatusType.Failed);
      // Set tables in updating status to failed.
      Set<String> tablesInPendingStatus =
          newXClusterConfig.getTableIdsInStatus(
              getTableIds(taskParams().getTableInfoList()),
              X_CLUSTER_TABLE_CONFIG_PENDING_STATUS_LIST);
      newXClusterConfig.updateStatusForTables(
          tablesInPendingStatus, XClusterTableConfig.Status.Failed);

      if (Objects.nonNull(currentXClusterConfig)) {
        // Load the xCluster from the DB again because it might be deleted.
        Optional<XClusterConfig> currentXClusterConfigOptional =
            XClusterConfig.maybeGet(currentXClusterConfig.getUuid());
        if (currentXClusterConfigOptional.isPresent()
            && !isInMustDeleteStatus(currentXClusterConfigOptional.get())) {
          currentXClusterConfigOptional.get().updateStatus(XClusterConfigStatusType.DeletionFailed);
        }
      }

      // Prevent all other DR tasks except delete from running.
      log.info(
          "Setting the dr config state of xCluster config {} to {} from {}",
          newXClusterConfig.getUuid(),
          State.Failed,
          drConfig.getState());
      drConfig.setState(State.Failed);
      drConfig.update();

      // Set backup and restore status to failed and alter load balanced.
      boolean isLoadBalancerAltered = false;
      for (Restore restore : restoreList) {
        isLoadBalancerAltered = isLoadBalancerAltered || restore.isAlterLoadBalancer();
      }
      handleFailedBackupAndRestore(
          backupList, restoreList, false /* isAbort */, isLoadBalancerAltered);
      throw new RuntimeException(e);
    } finally {
      // Unlock the source universe.
      unlockUniverseForUpdate(sourceUniverse.getUniverseUUID());
      drConfig.refresh();
      String message = taskSucceeded ? "Task Succeeded" : "Task Failed";
      kubernetesStatus.updateDrConfigStatus(drConfig, message, getUserTaskUUID());
    }

    log.info("Completed {}", getName());
  }

  /**
   * It adds subtasks to delete the current xCluster config and set up the new xCluster config. The
   * new xCluster config must be created in the controller level and all of its corresponding
   * taskParams to create the new xCluster config must be already set.
   *
   * @param currentXClusterConfig The current xCluster config that is going to be deleted
   * @param newXClusterConfig The new xCluster config that is going to be set up
   * @param forceDeleteCurrentXClusterConfig Whether to force delete the current xCluster Config
   */
  protected void addSubtasksToUseNewXClusterConfig(
      @Nullable XClusterConfig currentXClusterConfig,
      XClusterConfig newXClusterConfig,
      boolean forceDeleteCurrentXClusterConfig) {
    if (Objects.nonNull(currentXClusterConfig)) {
      // Delete the main replication config.
      createDeleteXClusterConfigSubtasks(
          currentXClusterConfig,
          false /* keepEntry */,
          forceDeleteCurrentXClusterConfig,
          false /* deleteSourcePitrConfigs */,
          true /* deleteTargetPitrConfigs */);
    }

    createPromoteSecondaryConfigToMainConfigTask(newXClusterConfig);

    createXClusterConfigSetStatusTask(newXClusterConfig, XClusterConfigStatusType.Updating);

    if (newXClusterConfig.getType() == XClusterConfig.ConfigType.Db) {
      addSubtasksToCreateXClusterConfig(
          newXClusterConfig, taskParams().getDbs(), taskParams().getPitrParams());
    } else {
      addSubtasksToCreateXClusterConfig(
          newXClusterConfig,
          taskParams().getTableInfoList(),
          taskParams().getMainTableIndexTablesMap(),
          taskParams().getSourceTableIdsWithNoTableOnTargetUniverse(),
          taskParams().getPitrParams());
    }

    // After all the other subtasks are done, set the DR states to show replication is happening.
    if (newXClusterConfig.isUsedForDr()) {
      createSetDrStatesTask(
              newXClusterConfig,
              State.Replicating,
              SourceUniverseState.ReplicatingData,
              TargetUniverseState.ReceivingData,
              null /* keyspacePending */)
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);
    }

    createXClusterConfigSetStatusTask(newXClusterConfig, XClusterConfigStatusType.Running)
        .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);
  }
}
