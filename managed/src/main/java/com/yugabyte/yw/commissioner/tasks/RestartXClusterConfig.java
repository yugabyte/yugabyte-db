// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.XClusterConfigModifyTables;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.models.Backup;
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
      lockUniverseForUpdate(sourceUniverse.getUniverseUUID(), sourceUniverse.getVersion());
      try {
        // Lock the target universe.
        lockUniverseForUpdate(targetUniverse.getUniverseUUID(), targetUniverse.getVersion());

        createCheckXUniverseAutoFlag(sourceUniverse, targetUniverse)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.PreflightChecks);

        // Set table type for old xCluster configs.
        xClusterConfig.updateTableType(taskParams().getTableInfoList());

        // Do not skip bootstrapping for the following tables. It will check if it is required.
        if (taskParams().getBootstrapParams() != null) {
          xClusterConfig.updateNeedBootstrapForTables(
              taskParams().getBootstrapParams().tables, true /* needBootstrap */);
        }

        createXClusterConfigSetStatusTask(XClusterConfig.XClusterConfigStatusType.Updating)
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
              getTableIds(taskParams().getTableInfoList()), XClusterTableConfig.Status.Updating);

          // Delete the replication group.
          createDeleteXClusterConfigSubtasks(
              xClusterConfig, true /* keepEntry */, taskParams().isForced());

          createXClusterConfigSetStatusTask(XClusterConfig.XClusterConfigStatusType.Updating);

          createXClusterConfigSetStatusForTablesTask(
              getTableIds(taskParams().getTableInfoList()), XClusterTableConfig.Status.Updating);

          addSubtasksToCreateXClusterConfig(
              sourceUniverse,
              targetUniverse,
              taskParams().getTableInfoList(),
              taskParams().getMainTableIndexTablesMap());
        } else {
          createXClusterConfigSetStatusForTablesTask(tableIds, XClusterTableConfig.Status.Updating);

          createXClusterConfigModifyTablesTask(
              tableIds, XClusterConfigModifyTables.Params.Action.REMOVE_FROM_REPLICATION_ONLY);

          createXClusterConfigSetStatusForTablesTask(tableIds, XClusterTableConfig.Status.Updating);

          addSubtasksToAddTablesToXClusterConfig(
              sourceUniverse,
              targetUniverse,
              taskParams().getTableInfoList(),
              taskParams().getMainTableIndexTablesMap(),
              tableIds);
        }

        createXClusterConfigSetStatusTask(XClusterConfig.XClusterConfigStatusType.Running)
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
        setXClusterConfigStatus(XClusterConfigStatusType.Failed);
      } else {
        setXClusterConfigStatus(XClusterConfigStatusType.Running);
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
      for (Backup backup : backupList) {
        if (backup.getBackupInfo().alterLoadBalancer) {
          isLoadBalancerAltered = true;
        }
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
