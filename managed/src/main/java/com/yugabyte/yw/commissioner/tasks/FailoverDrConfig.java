// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.common.DrConfigStates.SourceUniverseState;
import com.yugabyte.yw.common.DrConfigStates.State;
import com.yugabyte.yw.common.DrConfigStates.TargetUniverseState;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.models.DrConfig;
import com.yugabyte.yw.models.PitrConfig;
import com.yugabyte.yw.models.Restore;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import com.yugabyte.yw.models.XClusterTableConfig;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.concurrent.TimeUnit;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class FailoverDrConfig extends EditDrConfig {

  @Inject
  protected FailoverDrConfig(
      BaseTaskDependencies baseTaskDependencies, XClusterUniverseService xClusterUniverseService) {
    super(baseTaskDependencies, xClusterUniverseService);
  }

  @Override
  public String getName() {
    return String.format(
        "%s(uuid=%s,universe=%s,drConfig=%s)",
        this.getClass().getSimpleName(),
        taskParams().getDrConfig().getUuid(),
        taskParams().getUniverseUUID(),
        taskParams().getDrConfig());
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    DrConfig drConfig = getDrConfigFromTaskParams();
    Optional<XClusterConfig> currentXClusterConfigOptional =
        Optional.ofNullable(taskParams().getOldXClusterConfig());
    // For failover, handling failure in the middle of task execution is not yet supported, thus
    // the dr config should always have the current xCluster config object.
    if (!currentXClusterConfigOptional.isPresent()) {
      throw new IllegalStateException(
          "The old xCluster config does not exist and cannot do a failover");
    }
    XClusterConfig currentXClusterConfig = currentXClusterConfigOptional.get();
    XClusterConfig failoverXClusterConfig = getXClusterConfigFromTaskParams();
    Universe sourceUniverse =
        Universe.getOrBadRequest(currentXClusterConfig.getSourceUniverseUUID());
    Universe targetUniverse =
        Universe.getOrBadRequest(currentXClusterConfig.getTargetUniverseUUID());
    try {
      // Lock the source universe.
      lockAndFreezeUniverseForUpdate(
          sourceUniverse.getUniverseUUID(), sourceUniverse.getVersion(), null /* Txn callback */);
      try {
        // Lock the target universe.
        lockAndFreezeUniverseForUpdate(
            targetUniverse.getUniverseUUID(), targetUniverse.getVersion(), null /* Txn callback */);

        createSetDrStatesTask(
                currentXClusterConfig,
                State.FailoverInProgress,
                SourceUniverseState.DrFailed,
                TargetUniverseState.SwitchingToDrPrimary,
                null /* keyspacePending */)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

        // The source and target universes are swapped in `failoverXClusterConfig`, so set the
        // target universe state of that config which the source universe of the main config to
        // `DrFailed`.
        createSetDrStatesTask(
                failoverXClusterConfig,
                null /* drConfigState */,
                null /* sourceUniverseState */,
                TargetUniverseState.DrFailed,
                null /* keyspacePending */)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

        // Delete the main replication config.
        // MUST skip deleting pitr configs here, otherwise we will not be able to restore to
        // safetime.
        createDeleteXClusterConfigSubtasks(
            currentXClusterConfig,
            false /* keepEntry */,
            true /* forceDelete */,
            false /* deletePitrConfigs */);

        createPromoteSecondaryConfigToMainConfigTask(failoverXClusterConfig);

        // Use pitr to restore to the safetime for all DBs.
        getNamespaces(taskParams().getTableInfoList())
            .forEach(
                namespace -> {
                  Optional<PitrConfig> pitrConfigOptional =
                      PitrConfig.maybeGet(
                          targetUniverse.getUniverseUUID(),
                          failoverXClusterConfig.getTableTypeAsCommonType(),
                          namespace.getName());
                  if (!pitrConfigOptional.isPresent()) {
                    throw new IllegalStateException(
                        String.format(
                            "No PITR config for database %s.%s found on universe %s",
                            failoverXClusterConfig.getTableTypeAsCommonType(),
                            namespace.getName(),
                            targetUniverse.getUniverseUUID()));
                  }

                  // Todo: Ensure the PITR config exists on YBDB.

                  Long namespaceSafetimeEpochUs =
                      taskParams()
                          .getNamespaceIdSafetimeEpochUsMap()
                          .get(namespace.getId().toStringUtf8());
                  if (Objects.isNull(namespaceSafetimeEpochUs)) {
                    throw new IllegalArgumentException(
                        String.format(
                            "No safetime for namespace %s is specified in the taskparams",
                            namespace.getName()));
                  }

                  // Todo: Add a subtask group for the following.
                  createRestoreSnapshotScheduleTask(
                      targetUniverse,
                      pitrConfigOptional.get(),
                      TimeUnit.MICROSECONDS.toMillis(namespaceSafetimeEpochUs));
                });

        // Delete PITR on failover as standby is now the new primary and we have restored snapshot.
        List<PitrConfig> pitrConfigs = currentXClusterConfig.getPitrConfigs();
        for (PitrConfig pitrConfig : pitrConfigs) {
          createDeletePitrConfigTask(
              pitrConfig.getUuid(), targetUniverse.getUniverseUUID(), true /* ignoreErrors */);
        }

        createSetDrStatesTask(
                failoverXClusterConfig,
                State.Halted,
                null /* sourceUniverseState */,
                null /* targetUniverseState */,
                null /* keyspacePending */)
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
      if (currentXClusterConfigOptional.isPresent()) {
        // Load the xCluster from the DB again because it might be deleted.
        Optional<XClusterConfig> xClusterConfigOptional =
            XClusterConfig.maybeGet(currentXClusterConfigOptional.get().getUuid());
        if (xClusterConfigOptional.isPresent()
            && !isInMustDeleteStatus(xClusterConfigOptional.get())) {
          xClusterConfigOptional.get().updateStatus(XClusterConfigStatusType.DeletionFailed);
        }
      }
      // Set xCluster config status to failed.
      if (!failoverXClusterConfig.getStatus().equals(XClusterConfigStatusType.Initialized)) {
        failoverXClusterConfig.updateStatus(XClusterConfigStatusType.Failed);
      }
      // Set tables in updating status to failed.
      Set<String> tablesInPendingStatus =
          failoverXClusterConfig.getTableIdsInStatus(
              getTableIds(taskParams().getTableInfoList()),
              X_CLUSTER_TABLE_CONFIG_PENDING_STATUS_LIST);
      failoverXClusterConfig.updateStatusForTables(
          tablesInPendingStatus, XClusterTableConfig.Status.Failed);
      // Set backup and restore status to failed and alter load balanced.
      boolean isLoadBalancerAltered = false;
      for (Restore restore : restoreList) {
        isLoadBalancerAltered = isLoadBalancerAltered || restore.isAlterLoadBalancer();
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
