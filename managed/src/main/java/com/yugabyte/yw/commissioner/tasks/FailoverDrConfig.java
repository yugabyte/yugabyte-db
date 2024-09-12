// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.common.DrConfigStates.SourceUniverseState;
import com.yugabyte.yw.common.DrConfigStates.State;
import com.yugabyte.yw.common.DrConfigStates.TargetUniverseState;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.models.PitrConfig;
import com.yugabyte.yw.models.Restore;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.ConfigType;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import com.yugabyte.yw.models.XClusterTableConfig;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.concurrent.TimeUnit;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.master.MasterTypes.NamespaceIdentifierPB;

@Slf4j
@Retryable
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

    XClusterConfig currentXClusterConfig = taskParams().getOldXClusterConfig();
    // Either the task is created which means the old xCluster config exists or the task is retry-ed
    //  which means the old xCluster config can potentially be deleted.
    if (isFirstTry() && Objects.isNull(currentXClusterConfig)) {
      throw new IllegalStateException(
          "The old xCluster config does not exist and cannot do a failover");
    } else if (!isFirstTry() && Objects.isNull(currentXClusterConfig)) {
      log.warn("The old xCluster config got deleted in the previous run");
    }
    XClusterConfig failoverXClusterConfig = getXClusterConfigFromTaskParams();
    // In failoverXClusterConfig, the source and target universes are swapped.
    Universe sourceUniverse =
        Universe.getOrBadRequest(failoverXClusterConfig.getTargetUniverseUUID());
    Universe targetUniverse =
        Universe.getOrBadRequest(failoverXClusterConfig.getSourceUniverseUUID());
    try {
      // Lock the source universe.
      lockAndFreezeUniverseForUpdate(
          sourceUniverse.getUniverseUUID(), sourceUniverse.getVersion(), null /* Txn callback */);
      try {
        // Lock the target universe.
        lockAndFreezeUniverseForUpdate(
            targetUniverse.getUniverseUUID(), targetUniverse.getVersion(), null /* Txn callback */);

        if (!failoverXClusterConfig.getDrConfig().getState().equals(State.Halted)) {
          if (Objects.nonNull(currentXClusterConfig)) {
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
          }

          // Todo: remove the following subtask from all the tasks.
          createPromoteSecondaryConfigToMainConfigTask(failoverXClusterConfig);

          Set<NamespaceIdentifierPB> namespaces;
          if (failoverXClusterConfig.getType().equals(ConfigType.Db)) {
            namespaces = getNamespaces(this.ybService, targetUniverse, taskParams().getDbs());
          } else {
            namespaces = getNamespaces(taskParams().getTableInfoList());
          }

          // Use pitr to restore to the safetime for all DBs.
          namespaces.forEach(
              namespace -> {
                Optional<PitrConfig> pitrConfigOptional =
                    PitrConfig.maybeGet(
                        targetUniverse.getUniverseUUID(),
                        failoverXClusterConfig.getTableTypeAsCommonType(),
                        namespace.getName());
                if (pitrConfigOptional.isEmpty()) {
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
      } catch (Exception e) {
        log.error("{} hit error : {}", getName(), e.getMessage());
        if (Objects.nonNull(currentXClusterConfig)) {
          // Load the xCluster from the DB again because it might be deleted.
          Optional<XClusterConfig> xClusterConfigOptional =
              XClusterConfig.maybeGet(currentXClusterConfig.getUuid());
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
        // Unlock the target universe.
        unlockUniverseForUpdate(targetUniverse.getUniverseUUID());
      }
    } finally {
      // Unlock the source universe.
      unlockUniverseForUpdate(sourceUniverse.getUniverseUUID());
    }

    log.info("Completed {}", getName());
  }
}
