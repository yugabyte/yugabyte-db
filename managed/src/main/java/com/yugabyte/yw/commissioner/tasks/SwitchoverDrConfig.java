// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.DrConfigStates.SourceUniverseState;
import com.yugabyte.yw.common.DrConfigStates.State;
import com.yugabyte.yw.common.DrConfigStates.TargetUniverseState;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.models.Restore;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import com.yugabyte.yw.models.XClusterTableConfig;
import java.util.Objects;
import java.util.Set;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Retryable
@Abortable
public class SwitchoverDrConfig extends EditDrConfig {

  @Inject
  protected SwitchoverDrConfig(
      BaseTaskDependencies baseTaskDependencies, XClusterUniverseService xClusterUniverseService) {
    super(baseTaskDependencies, xClusterUniverseService);
  }

  @Override
  public String getName() {
    return String.format(
        "%s(uuid=%s,universe=%s,drConfig=%s)",
        getClass().getSimpleName(),
        taskParams().getDrConfig().getUuid(),
        taskParams().getUniverseUUID(),
        taskParams().getDrConfig());
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    XClusterConfig currentXClusterConfig = taskParams().getOldXClusterConfig();
    // Either the task is created which means the old xCluster config exists or the task is retry-ed
    //  which means the old xCluster config can potentially be deleted.
    if (isFirstTry() && Objects.isNull(currentXClusterConfig)) {
      throw new IllegalStateException(
          "The old xCluster config does not exist and cannot do a failover");
    } else if (!isFirstTry() && Objects.isNull(currentXClusterConfig)) {
      log.warn("The old xCluster config got deleted in the previous run");
    }
    XClusterConfig switchoverXClusterConfig = getXClusterConfigFromTaskParams();
    // In switchoverXClusterConfig, the source and target universes are swapped.
    Universe sourceUniverse =
        Universe.getOrBadRequest(switchoverXClusterConfig.getTargetUniverseUUID());
    Universe targetUniverse =
        Universe.getOrBadRequest(switchoverXClusterConfig.getSourceUniverseUUID());
    try {
      // Lock the source universe.
      lockAndFreezeUniverseForUpdate(
          sourceUniverse.getUniverseUUID(), sourceUniverse.getVersion(), null /* Txn callback */);
      try {
        // Lock the target universe.
        lockAndFreezeUniverseForUpdate(
            targetUniverse.getUniverseUUID(), targetUniverse.getVersion(), null /* Txn callback */);

        if (Objects.nonNull(currentXClusterConfig)) {
          if (switchoverXClusterConfig.getStatus() == XClusterConfigStatusType.Initialized) {
            // Swap the source and target universe.
            createCheckXUniverseAutoFlag(
                    targetUniverse,
                    sourceUniverse,
                    false /* checkAutoFlagsEqualityOnBothUniverses */)
                .setSubTaskGroupType(SubTaskGroupType.PreflightChecks);

            createSetDrStatesTask(
                    currentXClusterConfig,
                    State.SwitchoverInProgress,
                    SourceUniverseState.PreparingSwitchover,
                    null /* targetUniverseState */,
                    null /* keyspacePending */)
                .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

            createXClusterConfigSetStatusForTablesTask(
                switchoverXClusterConfig,
                getTableIds(taskParams().getTableInfoList()),
                XClusterTableConfig.Status.Updating);

            createXClusterConfigSetStatusTask(
                switchoverXClusterConfig, XClusterConfigStatusType.Updating);
          }

          if (switchoverXClusterConfig.getStatus() != XClusterConfigStatusType.Running) {
            // Create new primary -> old primary (this will set old primary's role to be STANDBY)
            if (switchoverXClusterConfig.getType() == XClusterConfig.ConfigType.Db) {
              addSubtasksToCreateXClusterConfig(
                  switchoverXClusterConfig, taskParams().getDbs(), taskParams().getPitrParams());
            } else {
              addSubtasksToCreateXClusterConfig(
                  switchoverXClusterConfig,
                  taskParams().getTableInfoList(),
                  taskParams().getMainTableIndexTablesMap(),
                  taskParams().getSourceTableIdsWithNoTableOnTargetUniverse(),
                  taskParams().getPitrParams());
            }

            createXClusterConfigSetStatusTask(
                    switchoverXClusterConfig, XClusterConfigStatusType.Running)
                .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);
          }

          if (currentXClusterConfig.getStatus() == XClusterConfigStatusType.Running) {

            createWaitForReplicationDrainTask(currentXClusterConfig);

            createXClusterConfigSetStatusTask(
                    currentXClusterConfig, XClusterConfigStatusType.DrainedData)
                .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);
          }

          // Todo: remove the following subtask from all the tasks.
          createPromoteSecondaryConfigToMainConfigTask(switchoverXClusterConfig);

          // Delete old primary -> new primary (this will set new primary's role to be ACTIVE)
          createDeleteXClusterConfigSubtasks(
              currentXClusterConfig,
              false /* keepEntry */,
              false /*forceDelete*/,
              true /*deletePitrConfigs*/);
        }

        // After all the other subtasks are done, set the DR states to show replication is
        // happening.
        createSetDrStatesTask(
                switchoverXClusterConfig,
                State.Replicating,
                SourceUniverseState.ReplicatingData,
                TargetUniverseState.ReceivingData,
                null /* keyspacePending */)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

        createMarkUniverseUpdateSuccessTasks(targetUniverse.getUniverseUUID())
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

        createMarkUniverseUpdateSuccessTasks(sourceUniverse.getUniverseUUID())
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

        getRunnableTask().runSubTasks();
      } catch (Exception e) {
        log.error("{} hit error : {}", getName(), e.getMessage());
        // Set tables in updating status to failed.
        Set<String> tablesInPendingStatus =
            switchoverXClusterConfig.getTableIdsInStatus(
                getTableIds(taskParams().getTableInfoList()),
                X_CLUSTER_TABLE_CONFIG_PENDING_STATUS_LIST);
        switchoverXClusterConfig.updateStatusForTables(
            tablesInPendingStatus, XClusterTableConfig.Status.Failed);
        // Set backup and restore status to failed and alter load balanced.
        boolean isLoadBalancerAltered = false;
        for (Restore restore : restoreList) {
          isLoadBalancerAltered = isLoadBalancerAltered || restore.isAlterLoadBalancer();
        }
        handleFailedBackupAndRestore(
            backupList, restoreList, false /* isAbort */, isLoadBalancerAltered);
        throw new RuntimeException(e);
      } finally {
        // Unlock the target universe.
        unlockUniverseForUpdate(targetUniverse.getUniverseUUID());
      }
    } finally {
      // Unlock the source universe.
      unlockUniverseForUpdate(sourceUniverse.getUniverseUUID());
    }

    log.info("Completed {}", getName());
  }
}
