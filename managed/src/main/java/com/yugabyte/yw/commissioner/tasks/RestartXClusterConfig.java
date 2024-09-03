// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.common.DrConfigStates.SourceUniverseState;
import com.yugabyte.yw.common.DrConfigStates.State;
import com.yugabyte.yw.common.DrConfigStates.TargetUniverseState;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.models.Restore;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import com.yugabyte.yw.models.XClusterTableConfig;
import java.util.Set;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class RestartXClusterConfig extends EditXClusterConfig {

  @Inject
  protected RestartXClusterConfig(
      BaseTaskDependencies baseTaskDependencies, XClusterUniverseService xClusterUniverseService) {
    super(baseTaskDependencies, xClusterUniverseService);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();
    Universe sourceUniverse = Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID());
    Universe targetUniverse = Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID());
    try {
      // Lock the source universe.
      lockAndFreezeUniverseForUpdate(
          sourceUniverse.getUniverseUUID(), sourceUniverse.getVersion(), null /* Txn callback */);
      try {
        // Lock the target universe.
        lockAndFreezeUniverseForUpdate(
            targetUniverse.getUniverseUUID(), targetUniverse.getVersion(), null /* Txn callback */);

        createCheckXUniverseAutoFlag(
                sourceUniverse, targetUniverse, false /* checkAutoFlagsEqualityOnBothUniverses */)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.PreflightChecks);

        // Set table type for old xCluster configs.
        xClusterConfig.updateTableType(taskParams().getTableInfoList());

        // Do not skip bootstrapping for the following tables. It will check if it is required.
        if (taskParams().getBootstrapParams() != null) {
          xClusterConfig.updateNeedBootstrapForTables(
              taskParams().getBootstrapParams().tables, true /* needBootstrap */);
        }

        createXClusterConfigSetStatusTask(
                xClusterConfig, XClusterConfig.XClusterConfigStatusType.Updating)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.DeleteXClusterReplication);

        Set<String> tableIds = getTableIds(taskParams().getTableInfoList());

        // A replication group with no tables in it cannot exist in YBDB. If all the tables must be
        // removed from the replication group, remove the replication group.
        boolean isRestartWholeConfig =
            xClusterConfig.getTableIdsWithReplicationSetup(tableIds, true /* done */).size()
                >= xClusterConfig.getTableIdsWithReplicationSetup().size();
        log.info("isRestartWholeConfig is {}", isRestartWholeConfig);
        if (isRestartWholeConfig) {
          createXClusterConfigSetStatusForTablesTask(
              xClusterConfig,
              getTableIds(taskParams().getTableInfoList()),
              XClusterTableConfig.Status.Updating);

          // Delete the replication group.
          createDeleteXClusterConfigSubtasks(
              xClusterConfig,
              true /* keepEntry */,
              taskParams().isForced(),
              false) /* deletePitrConfigs */;

          if (xClusterConfig.isUsedForDr()) {
            createSetDrStatesTask(
                    xClusterConfig,
                    State.Initializing,
                    SourceUniverseState.Unconfigured,
                    TargetUniverseState.Unconfigured,
                    null /* keyspacePending */)
                .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);
          }

          createXClusterConfigSetStatusTask(
              xClusterConfig, XClusterConfig.XClusterConfigStatusType.Updating);

          createXClusterConfigSetStatusForTablesTask(
              xClusterConfig,
              getTableIds(taskParams().getTableInfoList()),
              XClusterTableConfig.Status.Updating);

          addSubtasksToCreateXClusterConfig(
              xClusterConfig,
              taskParams().getTableInfoList(),
              taskParams().getMainTableIndexTablesMap(),
              taskParams().getSourceTableIdsWithNoTableOnTargetUniverse(),
              taskParams().getPitrParams(),
              taskParams().isForceBootstrap());
        } else {
          createXClusterConfigSetStatusForTablesTask(
              xClusterConfig, tableIds, XClusterTableConfig.Status.Updating);

          createRemoveTableFromXClusterConfigSubtasks(
              xClusterConfig, tableIds, true /* keepEntry */);

          createXClusterConfigSetStatusForTablesTask(
              xClusterConfig, tableIds, XClusterTableConfig.Status.Updating);

          addSubtasksToAddTablesToXClusterConfig(
              xClusterConfig,
              taskParams().getTableInfoList(),
              taskParams().getMainTableIndexTablesMap(),
              tableIds);
        }

        createXClusterConfigSetStatusTask(
                xClusterConfig, XClusterConfig.XClusterConfigStatusType.Running)
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

      // Set XClusterConfig status to Running if at least one table is running.
      Set<String> tablesInRunningStatus =
          xClusterConfig.getTableIdsInStatus(
              xClusterConfig.getTableIds(), XClusterTableConfig.Status.Running);
      if (tablesInRunningStatus.isEmpty()) {
        xClusterConfig.updateStatus(XClusterConfigStatusType.Failed);
      } else {
        xClusterConfig.updateStatus(XClusterConfigStatusType.Running);
      }
      // Set tables in updating status to failed.
      Set<String> tablesInUpdatingStatus =
          xClusterConfig.getTableIdsInStatus(
              getTableIds(taskParams().getTableInfoList()),
              X_CLUSTER_TABLE_CONFIG_PENDING_STATUS_LIST);
      xClusterConfig.updateStatusForTables(
          tablesInUpdatingStatus, XClusterTableConfig.Status.Failed);
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
