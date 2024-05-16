// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.common.DrConfigStates.SourceUniverseState;
import com.yugabyte.yw.common.DrConfigStates.State;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.models.DrConfig;
import com.yugabyte.yw.models.Restore;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import com.yugabyte.yw.models.XClusterTableConfig;
import java.util.Optional;
import java.util.Set;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
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
        this.getClass().getSimpleName(),
        taskParams().getDrConfig().getUuid(),
        taskParams().getUniverseUUID(),
        taskParams().getDrConfig());
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    DrConfig drConfig = getDrConfigFromTaskParams();
    Optional<XClusterConfig> currentXClusterConfigOptional =
        Optional.ofNullable(taskParams().getOldXClusterConfig());
    // For switchover, handling failure in the middle of task execution is not yet supported, thus
    // the dr config should always have the current xCluster config object.
    if (!currentXClusterConfigOptional.isPresent()) {
      throw new IllegalStateException(
          "The old xCluster config does not exist and cannot do a switchover");
    }
    XClusterConfig currentXClusterConfig = currentXClusterConfigOptional.get();
    XClusterConfig switchoverXClusterConfig = getXClusterConfigFromTaskParams();
    Universe sourceUniverse =
        Universe.getOrBadRequest(currentXClusterConfig.getSourceUniverseUUID());
    Universe targetUniverse =
        Universe.getOrBadRequest(currentXClusterConfig.getTargetUniverseUUID());
    try {
      // Lock the source universe.
      lockAndFreezeUniverseForUpdate(
          sourceUniverse.getUniverseUUID(), sourceUniverse.getVersion(), null /* Txn callback */);
      try {
        // Lock the target universe.
        lockAndFreezeUniverseForUpdate(
            targetUniverse.getUniverseUUID(), targetUniverse.getVersion(), null /* Txn callback */);

        createSetDrStatesTask(
                currentXClusterConfig,
                State.SwitchoverInProgress,
                SourceUniverseState.PreparingSwitchover,
                null /* targetUniverseState */,
                null /* keyspacePending */)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

        createXClusterConfigSetStatusTask(
            switchoverXClusterConfig, XClusterConfigStatusType.Updating);

        createXClusterConfigSetStatusForTablesTask(
            switchoverXClusterConfig,
            getTableIds(taskParams().getTableInfoList()),
            XClusterTableConfig.Status.Updating);

        // Create new primary -> old primary (this will set old primary's role to be STANDBY)
        addSubtasksToCreateXClusterConfig(
            switchoverXClusterConfig,
            taskParams().getTableInfoList(),
            taskParams().getMainTableIndexTablesMap(),
            taskParams().getSourceTableIdsWithNoTableOnTargetUniverse(),
            taskParams().getPitrParams());

        createWaitForReplicationDrainTask(currentXClusterConfig);

        createPromoteSecondaryConfigToMainConfigTask(switchoverXClusterConfig);

        // Delete old primary -> new primary (this will set new primary's role to be ACTIVE)
        createDeleteXClusterConfigSubtasks(
            currentXClusterConfig,
            false /* keepEntry */,
            false /*forceDelete*/,
            true /*deletePitrConfigs*/);

        createXClusterConfigSetStatusTask(
                switchoverXClusterConfig, XClusterConfigStatusType.Running)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

        createMarkUniverseUpdateSuccessTasks(targetUniverse.getUniverseUUID())
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

        createMarkUniverseUpdateSuccessTasks(sourceUniverse.getUniverseUUID())
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

        getRunnableTask().runSubTasks();
      } finally {
        // Unlock the target universe.
        unlockUniverseForUpdate(targetUniverse.getUniverseUUID());
      }
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      if (currentXClusterConfigOptional.isPresent()) {
        // Load the xCluster from the DB again because it might be deleted.
        Optional<XClusterConfig> xClusterConfigOptional =
            XClusterConfig.maybeGet(currentXClusterConfigOptional.get().getUuid());
        if (xClusterConfigOptional.isPresent()
            && !isInMustDeleteStatus(xClusterConfigOptional.get())) {
          xClusterConfigOptional.get().updateStatus(XClusterConfigStatusType.DeletionFailed);
        }
      }
      // Set xCluster config status to failed.
      if (!switchoverXClusterConfig.getStatus().equals(XClusterConfigStatusType.Initialized)) {
        switchoverXClusterConfig.updateStatus(XClusterConfigStatusType.Failed);
      }
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
      // Unlock the source universe.
      unlockUniverseForUpdate(sourceUniverse.getUniverseUUID());
    }

    log.info("Completed {}", getName());
  }
}
