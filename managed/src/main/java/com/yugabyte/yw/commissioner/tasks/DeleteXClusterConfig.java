// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import com.yugabyte.yw.models.helpers.CommonUtils;
import java.util.Collections;
import java.util.HashSet;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.annotation.Nullable;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class DeleteXClusterConfig extends XClusterConfigTaskBase {

  @Inject
  protected DeleteXClusterConfig(
      BaseTaskDependencies baseTaskDependencies, XClusterUniverseService xClusterUniverseService) {
    super(baseTaskDependencies, xClusterUniverseService);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();

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

        createDeleteXClusterConfigSubtasks(xClusterConfig, sourceUniverse, targetUniverse);

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

  protected void createDeleteXClusterConfigSubtasks(
      XClusterConfig xClusterConfig,
      @Nullable Universe sourceUniverse,
      @Nullable Universe targetUniverse) {
    if (!isInMustDeleteStatus(xClusterConfig)) {
      createXClusterConfigSetStatusTask(
              xClusterConfig, XClusterConfig.XClusterConfigStatusType.Updating)
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.DeleteXClusterReplication);
    }

    // Create all the subtasks to delete the xCluster config and all the bootstrap ids related
    // to them if any.
    createDeleteXClusterConfigSubtasks(
        xClusterConfig,
        false /* keepEntry */,
        taskParams().isForced(),
        true /* deletePitrConfigs */);

    // Fetch all universes that are connected through xCluster config to source and
    // target universe.
    Set<Universe> xClusterConnectedUniverseSet = new HashSet<>();
    Set<UUID> alreadyLockedUniverseUUIDSet = new HashSet<>();
    if (sourceUniverse != null) {
      xClusterConnectedUniverseSet.addAll(
          xClusterUniverseService.getXClusterConnectedUniverses(sourceUniverse));
      alreadyLockedUniverseUUIDSet.add(sourceUniverse.getUniverseUUID());
    }
    if (targetUniverse != null) {
      xClusterConnectedUniverseSet.addAll(
          xClusterUniverseService.getXClusterConnectedUniverses(targetUniverse));
      alreadyLockedUniverseUUIDSet.add(targetUniverse.getUniverseUUID());
    }

    if (xClusterConnectedUniverseSet.stream()
        .anyMatch(
            univ ->
                CommonUtils.isReleaseBefore(
                    Util.YBDB_ROLLBACK_DB_VERSION,
                    univ.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion))) {
      // Promote auto flags on all connected universes which were blocked
      // due to the xCluster config.
      createPromoteAutoFlagsAndLockOtherUniversesForUniverseSet(
          xClusterConnectedUniverseSet.stream()
              .map(Universe::getUniverseUUID)
              .collect(Collectors.toSet()),
          alreadyLockedUniverseUUIDSet,
          xClusterUniverseService,
          Collections.singleton(xClusterConfig.getUuid()),
          taskParams().isForced());
    }
  }
}
