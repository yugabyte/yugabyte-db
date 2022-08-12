// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import java.util.Optional;
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

    try {
      lockUniverseForUpdate(getUniverse().version);

      XClusterConfig xClusterConfig = getXClusterConfig();
      if (xClusterConfig == null) {
        throw new RuntimeException("xClusterConfig in task params cannot be null");
      }

      // Delete the replication CDC streams on the target universe.
      createXClusterConfigDeleteTask(true /* ignoreErrors */)
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

      Optional<Universe> targetUniverse = Universe.maybeGet(xClusterConfig.targetUniverseUUID);
      targetUniverse.ifPresent(
          universe ->
              createTransferXClusterCertsRemoveTasks(
                  universe.getNodes(), xClusterConfig.getReplicationGroupName()));
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);
      getRunnableTask().runSubTasks();

    } catch (Exception e) {
      if (maybeGetXClusterConfig().isPresent()) {
        setXClusterConfigStatus(XClusterConfigStatusType.Failed);
      }
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    } finally {
      unlockUniverseForUpdate();
    }

    log.info("Completed {}", getName());
  }
}
