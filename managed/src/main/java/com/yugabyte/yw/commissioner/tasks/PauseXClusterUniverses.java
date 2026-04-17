package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class PauseXClusterUniverses extends XClusterConfigTaskBase {

  @Inject
  protected PauseXClusterUniverses(
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

        // Used in createUpdateWalRetentionTasks.
        taskParams().setUniverseUUID(sourceUniverse.getUniverseUUID());
        taskParams().clusters = sourceUniverse.getUniverseDetails().clusters;
        createUpdateWalRetentionTasks(sourceUniverse, XClusterUniverseAction.PAUSE);

        createSetReplicationPausedTask(xClusterConfig, true /* pause */);

        createPauseUniverseTasks(
            sourceUniverse, Customer.get(sourceUniverse.getCustomerId()).getUuid());

        taskParams().setUniverseUUID(targetUniverse.getUniverseUUID());
        createPauseUniverseTasks(
            targetUniverse, Customer.get(targetUniverse.getCustomerId()).getUuid());

        createMarkUniverseUpdateSuccessTasks(sourceUniverse.getUniverseUUID())
            .setSubTaskGroupType(SubTaskGroupType.PauseUniverse);

        createMarkUniverseUpdateSuccessTasks(targetUniverse.getUniverseUUID())
            .setSubTaskGroupType(SubTaskGroupType.PauseUniverse);

        getRunnableTask().runSubTasks();

      } finally {
        // Unlock the target universe.
        unlockUniverseForUpdate(targetUniverse.getUniverseUUID());
      }
    } catch (Throwable t) {
      log.error("{} hit error : {}", getName(), t.getMessage());
      throw t;
    } finally {
      // Unlock the source universe.
      unlockUniverseForUpdate(sourceUniverse.getUniverseUUID());
    }
    log.info("Completed {}", getName());
  }
}
