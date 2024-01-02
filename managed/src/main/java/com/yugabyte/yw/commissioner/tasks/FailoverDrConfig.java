// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.common.DrConfigStates.SourceUniverseState;
import com.yugabyte.yw.common.DrConfigStates.State;
import com.yugabyte.yw.common.DrConfigStates.TargetUniverseState;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.forms.DrConfigFailoverForm;
import com.yugabyte.yw.forms.DrConfigFailoverForm.Type;
import com.yugabyte.yw.forms.DrConfigTaskParams;
import com.yugabyte.yw.models.DrConfig;
import com.yugabyte.yw.models.PitrConfig;
import com.yugabyte.yw.models.Restore;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import com.yugabyte.yw.models.XClusterTableConfig;
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
        "%s(uuid=%s,universe=%s,type=%s)",
        this.getClass().getSimpleName(),
        taskParams().getDrConfig().getUuid(),
        taskParams().getUniverseUUID(),
        taskParams().getFailoverType());
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    DrConfig drConfig = getDrConfigFromTaskParams();
    XClusterConfig currentXClusterConfig = drConfig.getActiveXClusterConfig();
    XClusterConfig failoverXClusterConfig = getXClusterConfigFromTaskParams();
    Universe sourceUniverse =
        Universe.getOrBadRequest(currentXClusterConfig.getSourceUniverseUUID());
    Universe targetUniverse =
        Universe.getOrBadRequest(currentXClusterConfig.getTargetUniverseUUID());
    DrConfigFailoverForm.Type failoverType = getFailoverTypeFromTaskParams();
    try {
      // Lock the source universe.
      lockUniverseForUpdate(sourceUniverse.getUniverseUUID(), sourceUniverse.getVersion());
      try {
        // Lock the target universe.
        lockUniverseForUpdate(targetUniverse.getUniverseUUID(), targetUniverse.getVersion());

        if (failoverType.equals(Type.PLANNED)) {
          // Todo: After DB support, put the old primary in read-only mode.

          createSetDrStatesTask(
                  currentXClusterConfig,
                  State.SwitchoverInProgress,
                  SourceUniverseState.PreparingSwitchover,
                  null /* targetUniverseState */)
              .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

          createWaitForReplicationDrainTask(currentXClusterConfig);

          addSubtasksToUseNewXClusterConfig(
              currentXClusterConfig,
              failoverXClusterConfig,
              false /* forceDeleteCurrentXClusterConfig */);
        } else if (failoverType.equals(Type.UNPLANNED)) {
          createSetDrStatesTask(
                  currentXClusterConfig,
                  State.FailoverInProgress,
                  SourceUniverseState.DrFailed,
                  TargetUniverseState.SwitchingToDrPrimary)
              .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

          // The source and target universes are swapped in `failoverXClusterConfig`, so set the
          // target universe state of that config which the source universe of the main config to
          // `DrFailed`.
          createSetDrStatesTask(
                  failoverXClusterConfig,
                  null /* drConfigState */,
                  null /* sourceUniverseState */,
                  TargetUniverseState.DrFailed)
              .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

          // Delete the main replication config.
          createDeleteXClusterConfigSubtasks(
              currentXClusterConfig, false /* keepEntry */, true /* forceDelete */);

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

          createSetDrStatesTask(
                  failoverXClusterConfig,
                  State.Halted,
                  null /* sourceUniverseState */,
                  null /* targetUniverseState */)
              .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);
        } else {
          throw new IllegalArgumentException(
              String.format("taskParams().failoverType is %s which is not known", failoverType));
        }

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
      // Load the xCluster from the DB again because it might be deleted.
      Optional<XClusterConfig> currentXClusterConfigOptional =
          XClusterConfig.maybeGet(currentXClusterConfig.getUuid());
      if (currentXClusterConfigOptional.isPresent()
          && !isInMustDeleteStatus(currentXClusterConfigOptional.get())) {
        currentXClusterConfigOptional.get().updateStatus(XClusterConfigStatusType.DeletionFailed);
      }
      // Set xCluster config status to failed.
      failoverXClusterConfig.updateStatus(XClusterConfigStatusType.Failed);
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

  protected DrConfigFailoverForm.Type getFailoverTypeFromTaskParams() {
    DrConfigTaskParams params = taskParams();
    if (Objects.isNull(params.getFailoverType())) {
      throw new IllegalArgumentException(
          "taskParams().failoverType cannot be null to run a FailoverDrConfig task");
    }
    return params.getFailoverType();
  }
}
