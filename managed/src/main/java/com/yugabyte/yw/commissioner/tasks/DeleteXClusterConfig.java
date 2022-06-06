// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class DeleteXClusterConfig extends XClusterConfigTaskBase {

  @Inject
  protected DeleteXClusterConfig(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    XClusterConfig xClusterConfig = getXClusterConfig();
    if (xClusterConfig == null) {
      throw new RuntimeException("xClusterConfig in task params cannot be null");
    }

    Universe sourceUniverse = Universe.getOrBadRequest(xClusterConfig.sourceUniverseUUID);
    Universe targetUniverse = Universe.getOrBadRequest(xClusterConfig.targetUniverseUUID);

    // Lock the source universe.
    lockUniverseForUpdate(sourceUniverse.universeUUID, sourceUniverse.version);
    try {
      // Lock the target universe.
      lockUniverseForUpdate(targetUniverse.universeUUID, targetUniverse.version);
      try {
        // Delete the replication CDC streams on the target universe.
        createDeleteReplicationTask(true /* ignoreErrors */)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

        // Delete bootstrap IDs created by bootstrap universe subtask.
        // forceDelete is true to prevent errors until the user can choose if they want forceDelete.
        createDeleteBootstrapIdsTask(true /* forceDelete */)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

        // Delete the source universe root cert from the target universe.
        createTransferXClusterCertsRemoveTasks(
            targetUniverse.getNodes(), xClusterConfig.getReplicationGroupName());

        // Delete the xCluster config from DB.
        createDeleteXClusterConfigFromDbTask()
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

        createMarkUniverseUpdateSuccessTasks(targetUniverse.universeUUID)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

        createMarkUniverseUpdateSuccessTasks(sourceUniverse.universeUUID)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

        getRunnableTask().runSubTasks();
      } catch (Exception e) {
        log.error("{} hit error : {}", getName(), e.getMessage());
        throw new RuntimeException(e);
      } finally {
        // Unlock the target universe.
        unlockUniverseForUpdate(targetUniverse.universeUUID);
      }
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      if (maybeGetXClusterConfig().isPresent()) {
        setXClusterConfigStatus(XClusterConfigStatusType.Failed);
      }
      throw new RuntimeException(e);
    } finally {
      // Unlock the source universe.
      unlockUniverseForUpdate(sourceUniverse.universeUUID);
    }

    log.info("Completed {}", getName());
  }
}
