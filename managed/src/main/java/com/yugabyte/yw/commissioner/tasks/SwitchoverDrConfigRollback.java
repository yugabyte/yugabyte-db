/*
 * Copyright 2024 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.CanRollback;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.common.DrConfigStates.SourceUniverseState;
import com.yugabyte.yw.common.DrConfigStates.State;
import com.yugabyte.yw.common.DrConfigStates.TargetUniverseState;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.models.Restore;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.ConfigType;
import com.yugabyte.yw.models.XClusterTableConfig;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.cdc.CdcConsumer.XClusterRole;

/**
 * Task to perform a roll back a switchover task of the DR config that has failed or aborted. This
 * task will make sure the old xCluster config is in Running statue while the new xCluster config is
 * deleted.
 */
@Slf4j
@Retryable
@Abortable
@CanRollback(enabled = false)
public class SwitchoverDrConfigRollback extends SwitchoverDrConfig {

  @Inject
  protected SwitchoverDrConfigRollback(
      BaseTaskDependencies baseTaskDependencies, XClusterUniverseService xClusterUniverseService) {
    super(baseTaskDependencies, xClusterUniverseService);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    XClusterConfig currentXClusterConfig = taskParams().getOldXClusterConfig();
    XClusterConfig switchoverXClusterConfig = getXClusterConfigFromTaskParams();
    UUID sourceUniverseUUID;
    UUID targetUniverseUUID;
    if (Objects.nonNull(currentXClusterConfig)) {
      sourceUniverseUUID = currentXClusterConfig.getSourceUniverseUUID();
      targetUniverseUUID = currentXClusterConfig.getTargetUniverseUUID();
    } else if (Objects.nonNull(switchoverXClusterConfig)) {
      // In switchoverXClusterConfig, the source and target universes are swapped.
      sourceUniverseUUID = switchoverXClusterConfig.getTargetUniverseUUID();
      targetUniverseUUID = switchoverXClusterConfig.getSourceUniverseUUID();
    } else {
      throw new IllegalStateException("Both old and new xCluster configs are null");
    }
    Universe sourceUniverse = Universe.getOrBadRequest(sourceUniverseUUID);
    Universe targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    try {
      // Lock the source universe.
      lockAndFreezeUniverseForUpdate(
          sourceUniverse.getUniverseUUID(), sourceUniverse.getVersion(), null /* Txn callback */);
      try {
        // Lock the target universe.
        lockAndFreezeUniverseForUpdate(
            targetUniverse.getUniverseUUID(), targetUniverse.getVersion(), null /* Txn callback */);

        // The previous xCluster config is already in Running state, we only need to delete the new
        // switchover xCluster config.
        if (Objects.nonNull(switchoverXClusterConfig)) {
          createDeleteXClusterConfigSubtasks(
              switchoverXClusterConfig,
              false /* keepEntry */,
              false /* forceDelete */,
              false /* deleteSourcePitrConfigs */,
              false /* deleteTargetPitrConfigs */);
        }

        if (currentXClusterConfig.getType() == ConfigType.Txn) {
          // Set the target universe role to Standby.
          createChangeXClusterRoleTask(
                  currentXClusterConfig,
                  null /* sourceRole */,
                  XClusterRole.STANDBY /* targetRole */,
                  false /* ignoreErrors */)
              .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);
        }

        createPromoteSecondaryConfigToMainConfigTask(currentXClusterConfig);

        createSetDrStatesTask(
                currentXClusterConfig,
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
