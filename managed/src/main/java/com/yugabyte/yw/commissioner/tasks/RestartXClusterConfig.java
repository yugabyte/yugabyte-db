// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import java.util.List;
import java.util.Map;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.master.MasterDdlOuterClass;

@Slf4j
public class RestartXClusterConfig extends CreateXClusterConfig {

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

        // Set needBootstrap to true for all tables. It will check if it is required.
        xClusterConfig.setNeedBootstrapForTables(
            xClusterConfig.getTables(), true /* needBootstrap */);

        Map<String, List<String>> mainTableIndexTablesMap =
            getMainTableIndexTablesMap(sourceUniverse, xClusterConfig.getTables());
        Map<String, List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo>>
            requestedNamespaceTablesInfoMap = checkTables(mainTableIndexTablesMap);

        createXClusterConfigSetStatusTask(XClusterConfig.XClusterConfigStatusType.Updating)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.DeleteXClusterReplication);

        // Delete the xCluster config.
        createDeleteXClusterConfigSubtasks(xClusterConfig, true /* keepEntry */);

        addSubtasksToCreateXClusterConfig(
            sourceUniverse,
            targetUniverse,
            requestedNamespaceTablesInfoMap,
            mainTableIndexTablesMap);

        getRunnableTask().runSubTasks();
      } finally {
        // Unlock the target universe.
        unlockUniverseForUpdate(targetUniverse.universeUUID);
      }
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      setXClusterConfigStatus(XClusterConfig.XClusterConfigStatusType.Failed);
      throw new RuntimeException(e);
    } finally {
      // Unlock the source universe.
      unlockUniverseForUpdate(sourceUniverse.universeUUID);
    }

    log.info("Completed {}", getName());
  }
}
