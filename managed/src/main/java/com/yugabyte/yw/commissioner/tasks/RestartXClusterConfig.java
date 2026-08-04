// Copyright (c) YugabyteDB, Inc.
package com.yugabyte.yw.commissioner.tasks;

import com.google.common.collect.Sets;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.common.DrConfigStates.SourceUniverseState;
import com.yugabyte.yw.common.DrConfigStates.State;
import com.yugabyte.yw.common.DrConfigStates.TargetUniverseState;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.operator.OperatorStatusUpdater;
import com.yugabyte.yw.common.operator.OperatorStatusUpdaterFactory;
import com.yugabyte.yw.models.DrConfig;
import com.yugabyte.yw.models.Restore;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.ConfigType;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import com.yugabyte.yw.models.XClusterNamespaceConfig;
import com.yugabyte.yw.models.XClusterTableConfig;
import java.util.Collections;
import java.util.List;
import java.util.Set;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.yb.master.MasterDdlOuterClass;

@Slf4j
@Retryable(enabled = false)
public class RestartXClusterConfig extends EditXClusterConfig {

  private final OperatorStatusUpdater kubernetesStatus;

  @Inject
  protected RestartXClusterConfig(
      BaseTaskDependencies baseTaskDependencies,
      XClusterUniverseService xClusterUniverseService,
      OperatorStatusUpdaterFactory operatorStatusUpdaterFactory) {
    super(baseTaskDependencies, xClusterUniverseService, operatorStatusUpdaterFactory);
    this.kubernetesStatus = operatorStatusUpdaterFactory.create();
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();
    Universe sourceUniverse = Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID());
    Universe targetUniverse = Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID());
    boolean taskSucceeded = false;
    try {
      // Lock the source universe.
      lockAndFreezeUniverseForUpdate(
          sourceUniverse.getUniverseUUID(), sourceUniverse.getVersion(), null /* Txn callback */);
      try {
        // Lock the target universe.
        lockAndFreezeUniverseForUpdate(
            targetUniverse.getUniverseUUID(), targetUniverse.getVersion(), null /* Txn callback */);

        createCheckXUniverseAutoFlag(
                sourceUniverse, targetUniverse, false /* checkAutoFlagsEqualityOnBothUniverses */)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.PreflightChecks);

        // Delete extra xCluster configs for DR configs.
        if (xClusterConfig.isUsedForDr()) {
          xClusterConfig.getDrConfig().getXClusterConfigs().stream()
              .filter(config -> !config.getUuid().equals(xClusterConfig.getUuid()))
              .forEach(
                  config ->
                      createDeleteXClusterConfigSubtasks(
                          config,
                          false /* keepEntry */,
                          true /* forceDelete */,
                          false /* deleteSourcePitrConfigs */,
                          false /* deleteTargetPitrConfigs */));
        }

        boolean isDBScopedReplication = xClusterConfig.getType() == ConfigType.Db;

        List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tableInfoList =
            taskParams().getTableInfoList();
        Set<String> tableIds = getTableIds(tableInfoList);

        boolean isRestartWholeConfig;
        if (!isDBScopedReplication) {
          // Set table type for old xCluster configs.
          xClusterConfig.updateTableType(tableInfoList);

          // Do not skip bootstrapping for the following tables. It will check if it is required.
          if (taskParams().getBootstrapParams() != null) {
            xClusterConfig.updateNeedBootstrapForTables(
                taskParams().getBootstrapParams().tables, true /* needBootstrap */);
          }

          createXClusterConfigSetStatusTask(
                  xClusterConfig, XClusterConfig.XClusterConfigStatusType.Updating)
              .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.DeleteXClusterReplication);

          // A replication group with no tables in it cannot exist in YBDB. If all the tables
          // must be removed from the replication group, remove the replication group.
          isRestartWholeConfig =
              Sets.difference(
                      xClusterConfig.getTableIdsWithReplicationSetup(),
                      xClusterConfig.getTableIdsWithReplicationSetup(tableIds, true /* done */))
                  .isEmpty();
        } else {
          isRestartWholeConfig =
              taskParams().getDbs().size() == xClusterConfig.getNamespaces().size();
        }

        if (!CollectionUtils.isEmpty(taskParams().getDbs())) {
          xClusterConfig.updateStatusForNamespaces(
              taskParams().getDbs(), XClusterNamespaceConfig.Status.Updating);
        }

        log.info("isRestartWholeConfig is {}", isRestartWholeConfig);
        if (isRestartWholeConfig) {
          if (!isDBScopedReplication) {
            createXClusterConfigSetStatusForTablesTask(
                xClusterConfig, getTableIds(tableInfoList), XClusterTableConfig.Status.Updating);
          }

          // Delete the replication group.
          createDeleteXClusterConfigSubtasks(
              xClusterConfig,
              true /* keepEntry */,
              taskParams().isForced(),
              false /* deleteSourcePitrConfigs */,
              false /* deleteTargetPitrConfigs */);

          if (xClusterConfig.isUsedForDr()) {
            createSetDrStatesTask(
                    xClusterConfig,
                    State.Initializing,
                    SourceUniverseState.Unconfigured,
                    TargetUniverseState.Unconfigured,
                    null /* keyspacePending */)
                .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);
          }

          createXClusterConfigSetStatusTask(
              xClusterConfig, XClusterConfig.XClusterConfigStatusType.Updating);

          if (isDBScopedReplication) {
            addSubtasksToCreateXClusterConfig(
                xClusterConfig,
                taskParams().getDbs(),
                taskParams().getPitrParams(),
                taskParams().isForceBootstrap());
          } else {
            createXClusterConfigSetStatusForTablesTask(
                xClusterConfig, getTableIds(tableInfoList), XClusterTableConfig.Status.Updating);

            addSubtasksToCreateXClusterConfig(
                xClusterConfig,
                tableInfoList,
                taskParams().getMainTableIndexTablesMap(),
                taskParams().getSourceTableIdsWithNoTableOnTargetUniverse(),
                null,
                taskParams().getPitrParams(),
                taskParams().isForceBootstrap());
          }

          // After all the other subtasks are done, set the DR states to show replication is
          // happening.
          if (xClusterConfig.isUsedForDr()) {
            createSetDrStatesTask(
                    xClusterConfig,
                    State.Replicating,
                    SourceUniverseState.ReplicatingData,
                    TargetUniverseState.ReceivingData,
                    null /* keyspacePending */)
                .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);
          }
        } else {
          if (!isDBScopedReplication) {
            createXClusterConfigSetStatusForTablesTask(
                xClusterConfig, tableIds, XClusterTableConfig.Status.Updating);

            createRemoveTableFromXClusterConfigSubtasks(
                xClusterConfig, tableIds, true /* keepEntry */);

            addSubtasksToAddTablesToXClusterConfig(
                xClusterConfig, tableInfoList, taskParams().getMainTableIndexTablesMap(), tableIds);
          } else {
            createXClusterConfigSetStatusTask(
                xClusterConfig, XClusterConfig.XClusterConfigStatusType.Updating);

            addSubtasksToRemoveDatabasesFromXClusterConfig(
                xClusterConfig, taskParams().getDbs(), true /* keepEntry */);

            addSubtasksToAddDatabasesToXClusterConfig(xClusterConfig, taskParams().getDbs());
          }
        }

        createXClusterConfigSetStatusTask(
                xClusterConfig, XClusterConfig.XClusterConfigStatusType.Running)
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
      taskSucceeded = true;
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());

      if (xClusterConfig.getType().equals(XClusterConfig.ConfigType.Db)) {
        // Set XClusterConfig status to Running if at least one namespace is running.
        Set<String> namespacesInRunningStatus =
            xClusterConfig.getNamespaceIdsInStatus(
                xClusterConfig.getDbIds(),
                Collections.singleton(XClusterNamespaceConfig.Status.Running));
        if (namespacesInRunningStatus.isEmpty()) {
          xClusterConfig.updateStatus(XClusterConfigStatusType.Failed);
        } else {
          xClusterConfig.updateStatus(XClusterConfigStatusType.Running);
        }
        // Set namespaces in updating status to failed.
        Set<String> namespacesInUpdatingStatus =
            xClusterConfig.getNamespaceIdsInStatus(
                taskParams().getDbs(), X_CLUSTER_NAMESPACE_CONFIG_PENDING_STATUS_LIST);
        xClusterConfig.updateStatusForNamespaces(
            namespacesInUpdatingStatus, XClusterNamespaceConfig.Status.Failed);
      } else {

        // Set XClusterConfig status to Running if at least one table is running.
        Set<String> tablesInRunningStatus =
            xClusterConfig.getTableIdsInStatus(
                xClusterConfig.getTableIds(), XClusterTableConfig.Status.Running);
        if (tablesInRunningStatus.isEmpty()) {
          xClusterConfig.updateStatus(XClusterConfigStatusType.Failed);
        } else {
          xClusterConfig.updateStatus(XClusterConfigStatusType.Running);
        }
        // Set tables in updating status to failed.
        Set<String> tablesInUpdatingStatus =
            xClusterConfig.getTableIdsInStatus(
                getTableIds(taskParams().getTableInfoList()),
                X_CLUSTER_TABLE_CONFIG_PENDING_STATUS_LIST);
        xClusterConfig.updateStatusForTables(
            tablesInUpdatingStatus, XClusterTableConfig.Status.Failed);
      }
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
      if (xClusterConfig.isUsedForDr()) {
        DrConfig drConfig = xClusterConfig.getDrConfig();
        drConfig.refresh();
        String message = taskSucceeded ? "Task Succeeded" : "Task Failed";
        kubernetesStatus.updateDrConfigStatus(drConfig, message, getUserTaskUUID());
      }
    }

    log.info("Completed {}", getName());
  }
}
