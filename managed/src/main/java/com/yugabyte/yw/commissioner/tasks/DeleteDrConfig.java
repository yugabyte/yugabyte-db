// Copyright (c) YugabyteDB, Inc.
package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.forms.DrConfigTaskParams;
import com.yugabyte.yw.models.DrConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import java.util.Objects;
import java.util.Optional;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Abortable
@Retryable
public class DeleteDrConfig extends DeleteXClusterConfig {

  @Inject
  protected DeleteDrConfig(
      BaseTaskDependencies baseTaskDependencies, XClusterUniverseService xClusterUniverseService) {
    super(baseTaskDependencies, xClusterUniverseService);
  }

  @Override
  protected DrConfigTaskParams taskParams() {
    return (DrConfigTaskParams) taskParams;
  }

  @Override
  public String getName() {
    if (taskParams().getDrConfig() != null) {
      return String.format(
          "%s(uuid=%s, universe=%s)",
          this.getClass().getSimpleName(),
          taskParams().getDrConfig().getUuid(),
          taskParams().getUniverseUUID());
    } else {
      return String.format(
          "%s(universe=%s)", this.getClass().getSimpleName(), taskParams().getUniverseUUID());
    }
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    DrConfig drConfig = taskParams().getDrConfig();
    XClusterConfig xClusterConfig = null;
    if (Objects.nonNull(drConfig)) {
      xClusterConfig = drConfig.getActiveXClusterConfig();
    }

    Universe sourceUniverse = null;
    if (Objects.nonNull(taskParams().getSourceUniverseUuid())) {
      sourceUniverse = Universe.maybeGet(taskParams().getSourceUniverseUuid()).orElse(null);
    }
    Universe targetUniverse = null;
    if (Objects.nonNull(taskParams().getTargetUniverseUuid())) {
      targetUniverse = Universe.maybeGet(taskParams().getTargetUniverseUuid()).orElse(null);
    }
    try {
      if (sourceUniverse != null) {
        // Lock the source universe.
        lockAndFreezeUniverseForUpdate(
            sourceUniverse.getUniverseUUID(), sourceUniverse.getVersion(), null /* Txn callback */);
      }
      try {
        if (targetUniverse != null) {
          // Lock the target universe.
          lockAndFreezeUniverseForUpdate(
              targetUniverse.getUniverseUUID(),
              targetUniverse.getVersion(),
              null /* Txn callback */);
        }

        if (Objects.nonNull(drConfig)) {
          for (XClusterConfig xcc : drConfig.getXClusterConfigs()) {
            if (!xcc.getUuid().equals(xClusterConfig.getUuid())) {
              createDeleteXClusterConfigSubtasks(
                  xcc,
                  Objects.nonNull(xcc.getSourceUniverseUUID())
                      ? Universe.maybeGet(xcc.getSourceUniverseUUID()).orElse(null)
                      : null,
                  Objects.nonNull(xcc.getTargetUniverseUUID())
                      ? Universe.maybeGet(xcc.getTargetUniverseUUID()).orElse(null)
                      : null);
            }
          }
        }

        // Delete the active xCluster config last.
        if (Objects.nonNull(xClusterConfig)) {
          createDeleteXClusterConfigSubtasks(xClusterConfig, sourceUniverse, targetUniverse);
        }

        // When the last xCluster config associated with this DR config is deleted, the dr config
        // entry will be deleted as well.

        if (targetUniverse != null) {
          createMarkUniverseUpdateSuccessTasks(targetUniverse.getUniverseUUID())
              .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);
        }

        if (sourceUniverse != null) {
          createMarkUniverseUpdateSuccessTasks(sourceUniverse.getUniverseUUID())
              .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);
        }

        getRunnableTask().runSubTasks();
      } finally {
        if (targetUniverse != null) {
          // Unlock the target universe.
          unlockUniverseForUpdate(targetUniverse.getUniverseUUID());
        }
        unlockXClusterUniverses(lockedXClusterUniversesUuidSet, false /* force delete */);
      }
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      Optional<XClusterConfig> mightDeletedXClusterConfig = maybeGetXClusterConfig();
      if (mightDeletedXClusterConfig.isPresent()
          && !isInMustDeleteStatus(mightDeletedXClusterConfig.get())) {
        mightDeletedXClusterConfig.get().updateStatus(XClusterConfigStatusType.DeletionFailed);
      }
      throw new RuntimeException(e);
    } finally {
      if (sourceUniverse != null) {
        // Unlock the source universe.
        unlockUniverseForUpdate(sourceUniverse.getUniverseUUID());
      }
    }

    log.info("Completed {}", getName());
  }
}
