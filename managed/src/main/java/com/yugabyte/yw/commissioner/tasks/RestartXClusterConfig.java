// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.XClusterConfigModifyTables;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.ConfigType;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import com.yugabyte.yw.models.XClusterTableConfig;
import java.util.Collections;
import java.util.Set;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class RestartXClusterConfig extends EditXClusterConfig {

  @Inject
  protected RestartXClusterConfig(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();
    Universe sourceUniverse = Universe.getOrBadRequest(xClusterConfig.sourceUniverseUUID);
    Universe targetUniverse = Universe.getOrBadRequest(xClusterConfig.targetUniverseUUID);
    try {
      // Lock the source universe.
      lockUniverseForUpdate(sourceUniverse.universeUUID, sourceUniverse.version);
      try {
        // Lock the target universe.
        lockUniverseForUpdate(targetUniverse.universeUUID, targetUniverse.version);

        // Set table type for old xCluster configs.
        xClusterConfig.setTableType(taskParams().getTableInfoList());

        // Do not skip bootstrapping for the following tables. It will check if it is required.
        if (xClusterConfig.type.equals(ConfigType.Txn)) {
          xClusterConfig.setNeedBootstrapForTables(
              getTableIds(Collections.singleton(taskParams().getTxnTableInfo())),
              true /* needBootstrap */);
        }
        if (taskParams().getBootstrapParams() != null) {
          xClusterConfig.setNeedBootstrapForTables(
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
              getTableIds(taskParams().getTableInfoList(), taskParams().getTxnTableInfo()),
              XClusterTableConfig.Status.Updating);

          // Delete the replication group.
          createDeleteXClusterConfigSubtasks(
              xClusterConfig, true /* keepEntry */, taskParams().isForced());

          createXClusterConfigSetStatusTask(XClusterConfig.XClusterConfigStatusType.Updating);

          createXClusterConfigSetStatusForTablesTask(
              getTableIds(taskParams().getTableInfoList(), taskParams().getTxnTableInfo()),
              XClusterTableConfig.Status.Updating);

          addSubtasksToCreateXClusterConfig(
              sourceUniverse,
              targetUniverse,
              taskParams().getTableInfoList(),
              taskParams().getMainTableIndexTablesMap(),
              taskParams().getTxnTableInfo());
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
              tableIds,
              taskParams().getTxnTableInfo());
        }

        createXClusterConfigSetStatusTask(XClusterConfig.XClusterConfigStatusType.Running)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

        createMarkUniverseUpdateSuccessTasks(targetUniverse.universeUUID)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

        createMarkUniverseUpdateSuccessTasks(sourceUniverse.universeUUID)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

        getRunnableTask().runSubTasks();
      } finally {
        // Unlock the target universe.
        unlockUniverseForUpdate(targetUniverse.universeUUID);
      }
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());

      // Set XClusterConfig status to Running if at least one table is running.
      Set<String> tablesInRunningStatus =
          xClusterConfig.getTableIdsInStatus(
              xClusterConfig.getTables(), XClusterTableConfig.Status.Running);
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
      xClusterConfig.setStatusForTables(tablesInUpdatingStatus, XClusterTableConfig.Status.Failed);
      throw new RuntimeException(e);
    } finally {
      // Unlock the source universe.
      unlockUniverseForUpdate(sourceUniverse.universeUUID);
    }

    log.info("Completed {}", getName());
  }
}
