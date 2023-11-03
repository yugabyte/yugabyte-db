// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
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
    XClusterConfig xClusterConfig = drConfig.getActiveXClusterConfig();

    Universe sourceUniverse = null;
    Universe targetUniverse = null;
    if (xClusterConfig.getSourceUniverseUUID() != null) {
      sourceUniverse = Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID());
    }
    if (xClusterConfig.getTargetUniverseUUID() != null) {
      targetUniverse = Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID());
    }
    try {
      if (sourceUniverse != null) {
        // Lock the source universe.
        lockUniverseForUpdate(sourceUniverse.getUniverseUUID(), sourceUniverse.getVersion());
      }
      try {
        if (targetUniverse != null) {
          // Lock the target universe.
          lockUniverseForUpdate(targetUniverse.getUniverseUUID(), targetUniverse.getVersion());
        }

        for (XClusterConfig xcc : drConfig.getXClusterConfigs()) {
          createDeleteXClusterConfigSubtasks(
              xcc,
              Objects.nonNull(xClusterConfig.getSourceUniverseUUID())
                  ? Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID())
                  : null,
              Objects.nonNull(xClusterConfig.getTargetUniverseUUID())
                  ? Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID())
                  : null);
        }

        createDeleteDrConfigEntryTask(drConfig)
            .setSubTaskGroupType(SubTaskGroupType.DeleteDrConfig);

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
