// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.XClusterConfigModifyTables;
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
  protected RestartXClusterConfig(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
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

        createXClusterConfigSetStatusTask(XClusterConfig.XClusterConfigStatusType.Updating)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.DeleteXClusterReplication);

        Set<String> tableIds = getTableIds(taskParams().getTableInfoList());

        // A replication group with no tables in it cannot exist in YBDB. If all the tables must be
        // removed from the replication group, remove the replication group.
        boolean isRestartWholeConfig =
            tableIds.size() >= xClusterConfig.getTableIdsWithReplicationSetup().size();

        createXClusterConfigSetStatusForTablesTask(tableIds, XClusterTableConfig.Status.Updating);

        if (isRestartWholeConfig) {
          // Delete the xCluster config.
          createDeleteXClusterConfigSubtasks(
              xClusterConfig, true /* keepEntry */, taskParams().isForced());

          createXClusterConfigSetStatusTask(XClusterConfig.XClusterConfigStatusType.Updating);

          createXClusterConfigSetStatusForTablesTask(tableIds, XClusterTableConfig.Status.Updating);

          addSubtasksToCreateXClusterConfig(
              sourceUniverse,
              targetUniverse,
              taskParams().getTableInfoList(),
              taskParams().getMainTableIndexTablesMap());
        } else {
          createXClusterConfigModifyTablesTask(
              tableIds, XClusterConfigModifyTables.Params.Action.REMOVE_FROM_REPLICATION_ONLY);

          createXClusterConfigSetStatusForTablesTask(tableIds, XClusterTableConfig.Status.Updating);

          addSubtasksToAddTablesToXClusterConfig(
              sourceUniverse,
              targetUniverse,
              taskParams().getTableInfoList(),
              taskParams().getMainTableIndexTablesMap());
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
              getTableIds(taskParams().getTableInfoList()), XClusterTableConfig.Status.Updating);
      xClusterConfig.setStatusForTables(tablesInUpdatingStatus, XClusterTableConfig.Status.Failed);
      throw new RuntimeException(e);
    } finally {
      // Unlock the source universe.
      unlockUniverseForUpdate(sourceUniverse.getUniverseUUID());
    }

    log.info("Completed {}", getName());
  }
}
