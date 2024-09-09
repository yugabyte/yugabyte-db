// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.common.DrConfigStates.SourceUniverseState;
import com.yugabyte.yw.common.DrConfigStates.State;
import com.yugabyte.yw.common.DrConfigStates.TargetUniverseState;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.table.TableInfoUtil;
import com.yugabyte.yw.forms.XClusterConfigEditFormData;
import com.yugabyte.yw.models.Restore;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.ConfigType;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import com.yugabyte.yw.models.XClusterNamespaceConfig;
import com.yugabyte.yw.models.XClusterTableConfig;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.springframework.util.CollectionUtils;
import org.yb.CommonTypes;
import org.yb.master.MasterDdlOuterClass;
import org.yb.master.MasterDdlOuterClass.ListTablesResponsePB.TableInfo;

@Slf4j
public class EditXClusterConfig extends CreateXClusterConfig {

  @Inject
  protected EditXClusterConfig(
      BaseTaskDependencies baseTaskDependencies, XClusterUniverseService xClusterUniverseService) {
    super(baseTaskDependencies, xClusterUniverseService);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();
    Universe sourceUniverse = Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID());
    Universe targetUniverse = Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID());
    XClusterConfigEditFormData editFormData = taskParams().getEditFormData();

    // Lock the source universe.
    lockAndFreezeUniverseForUpdate(
        sourceUniverse.getUniverseUUID(), sourceUniverse.getVersion(), null /* Txn callback */);
    try {
      // Lock the target universe.
      lockAndFreezeUniverseForUpdate(
          targetUniverse.getUniverseUUID(), targetUniverse.getVersion(), null /* Txn callback */);
      try {

        // Check Auto flags on source and target universes while resuming xCluster.
        if (editFormData.status != null && editFormData.status.equals("Running")) {
          createCheckXUniverseAutoFlag(
                  sourceUniverse, targetUniverse, false /* checkAutoFlagsEqualityOnBothUniverses */)
              .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.PreflightChecks);
        }

        createXClusterConfigSetStatusTask(xClusterConfig, XClusterConfigStatusType.Updating)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

        if (editFormData.name != null) {
          String oldReplicationGroupName = xClusterConfig.getReplicationGroupName();
          // If TLS root certificates are different, create a directory containing the source
          // universe root certs with the new name.
          createTransferXClusterCertsCopyTasks(
              xClusterConfig,
              xClusterConfig.getNewReplicationGroupName(
                  xClusterConfig.getSourceUniverseUUID(), editFormData.name));

          createXClusterConfigRenameTask(xClusterConfig, editFormData.name)
              .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

          // Delete the old directory if it created a new one. When the old directory is removed
          // because of renaming, the directory for transactional replication must not be deleted.
          createTransferXClusterCertsRemoveTasks(xClusterConfig, oldReplicationGroupName);
        } else if (editFormData.status != null) {
          createSetReplicationPausedTask(xClusterConfig, editFormData.status)
              .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);
        } else if (editFormData.sourceRole != null || editFormData.targetRole != null) {
          createChangeXClusterRoleTask(
                  taskParams().getXClusterConfig(),
                  editFormData.sourceRole,
                  editFormData.targetRole)
              .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);
        } else if (editFormData.tables != null) {
          if (!CollectionUtils.isEmpty(taskParams().getTableInfoList())) {
            createSubTaskToAddTables(xClusterConfig);
          }
          if (!CollectionUtils.isEmpty(taskParams().getTableIdsToRemove())) {
            createSubTaskToRemoveTables(xClusterConfig, sourceUniverse);
          }
        } else if (editFormData.databases != null) { // Used for DB scoped replication only.
          if (!xClusterConfig.getType().equals(ConfigType.Db)) {
            throw new IllegalArgumentException(
                "The databases must be provided only for DB scoped replication");
          }
          Set<String> databaseIdsToAdd = taskParams().getDatabaseIdsToAdd();
          Set<String> databaseIdsToRemove = taskParams().getDatabaseIdsToRemove();
          log.info("The databases to remove are {}", databaseIdsToRemove);
          if (!databaseIdsToAdd.isEmpty()) {
            addSubtasksToAddDatabasesToXClusterConfig(xClusterConfig, databaseIdsToAdd);
          }
          if (!databaseIdsToRemove.isEmpty()) {
            addSubtasksToRemoveDatabasesFromXClusterConfig(xClusterConfig, databaseIdsToRemove);
          }

        } else {
          throw new RuntimeException("No edit operation was specified in editFormData");
        }

        createXClusterConfigSetStatusTask(xClusterConfig, XClusterConfigStatusType.Running)
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
      xClusterConfig.updateStatus(XClusterConfigStatusType.Running);
      if (editFormData.tables != null) {
        // Set tables in updating status to failed.
        Set<String> tablesInPendingStatus =
            xClusterConfig.getTableIdsInStatus(
                Stream.concat(
                        getTableIds(taskParams().getTableInfoList()).stream(),
                        taskParams().getTableIdsToRemove().stream())
                    .collect(Collectors.toSet()),
                X_CLUSTER_TABLE_CONFIG_PENDING_STATUS_LIST);
        xClusterConfig.updateStatusForTables(
            tablesInPendingStatus, XClusterTableConfig.Status.Failed);
      }
      if (editFormData.databases != null) {
        // Set databases in updating status to failed.
        Set<String> dbIds = editFormData.databases;
        Set<String> namespacesInPendingStatus =
            xClusterConfig.getNamespaceIdsInStatus(
                dbIds, X_CLUSTER_NAMESPACE_CONFIG_PENDING_STATUS_LIST);
        xClusterConfig.updateStatusForNamespaces(
            namespacesInPendingStatus, XClusterNamespaceConfig.Status.Failed);
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
    }

    log.info("Completed {}", getName());
  }

  protected void createSubTaskToAddTables(XClusterConfig xClusterConfig) {
    createXClusterConfigSetStatusForTablesTask(
        xClusterConfig, taskParams().getTableIdsToAdd(), XClusterTableConfig.Status.Updating);

    addSubtasksToAddTablesToXClusterConfig(
        xClusterConfig,
        taskParams().getTableInfoList(),
        taskParams().getMainTableIndexTablesMap(),
        Collections.emptySet() /* tableIdsScheduledForBeingRemoved */);
  }

  protected void createSubTaskToRemoveTables(
      XClusterConfig xClusterConfig, Universe sourceUniverse) {
    createXClusterConfigSetStatusForTablesTask(
        xClusterConfig, taskParams().getTableIdsToRemove(), XClusterTableConfig.Status.Updating);

    Set<String> databaseNamesToBeDropped = new HashSet<>();
    List<TableInfo> tableInfoList =
        getTableInfoList(ybService, sourceUniverse, xClusterConfig.getTableIds());
    Map<String, List<TableInfo>> namespaceTableMap =
        XClusterConfigTaskBase.groupByNamespaceName(tableInfoList);
    for (Map.Entry<String, List<TableInfo>> entry : namespaceTableMap.entrySet()) {
      boolean deleteAllTables = true;
      for (TableInfo tableInfo : entry.getValue()) {
        if (!taskParams().getTableIdsToRemove().contains(tableInfo.getId().toStringUtf8())) {
          deleteAllTables = false;
          break;
        }
      }
      if (deleteAllTables) {
        databaseNamesToBeDropped.add(entry.getKey());
      }
    }

    createRemoveTableFromXClusterConfigSubtasks(
        xClusterConfig,
        taskParams().getTableIdsToRemove(),
        false /* keepEntry */,
        databaseNamesToBeDropped);
  }

  protected void addSubtasksToAddTablesToXClusterConfig(
      XClusterConfig xClusterConfig,
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableInfoList,
      Map<String, List<String>> mainTableIndexTablesMap,
      Set<String> tableIdsScheduledForBeingRemoved) {

    Map<String, List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo>>
        dbToTablesInfoMapNeedBootstrap =
            getDbToTablesInfoMapNeedBootstrapAndUpdateTableNeedBootstrap(
                xClusterConfig,
                taskParams().getTableIdsToAdd(),
                requestedTableInfoList,
                mainTableIndexTablesMap,
                taskParams().getSourceTableIdsWithNoTableOnTargetUniverse(),
                false /*isForceBootstrap*/);

    // Add the subtasks to set up replication for tables that do not need bootstrapping.
    Set<String> tableIdsNotNeedBootstrap =
        getTableIdsNotNeedBootstrap(taskParams().getTableIdsToAdd());
    if (!tableIdsNotNeedBootstrap.isEmpty()) {
      addSubtasksForTablesNotNeedBootstrap(
          xClusterConfig,
          tableIdsNotNeedBootstrap,
          requestedTableInfoList,
          null,
          true /* isReplicationConfigCreated */,
          taskParams().getPitrParams());

      if (xClusterConfig.isUsedForDr()) {
        createSetDrStatesTask(
                xClusterConfig,
                State.Replicating,
                SourceUniverseState.ReplicatingData,
                TargetUniverseState.ReceivingData,
                null /* keyspacePending */)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);
      }
    }

    // Add the subtasks to set up replication for tables that need bootstrapping if any.
    if (!dbToTablesInfoMapNeedBootstrap.isEmpty()) {

      Set<String> tableIdsDeleteReplication =
          getTableIdsToDeleteInReplicationDueToBootstrap(
              xClusterConfig,
              dbToTablesInfoMapNeedBootstrap,
              mainTableIndexTablesMap,
              tableIdsScheduledForBeingRemoved);

      Set<String> tableIdsWithReplicationSetup = xClusterConfig.getTableIdsWithReplicationSetup();

      // A replication group with no tables in it cannot exist in YBDB. If all the tables must be
      // removed from the replication group, remove the replication group.
      boolean isRestartWholeConfig =
          tableIdsDeleteReplication.size() + tableIdsScheduledForBeingRemoved.size()
              >= tableIdsWithReplicationSetup.size() + tableIdsNotNeedBootstrap.size();
      log.info(
          "tableIdsDeleteReplication is {} and isRestartWholeConfig is {}",
          tableIdsDeleteReplication,
          isRestartWholeConfig);
      if (isRestartWholeConfig) {
        createXClusterConfigSetStatusForTablesTask(
            xClusterConfig,
            getTableIds(requestedTableInfoList),
            XClusterTableConfig.Status.Updating);

        // Delete the xCluster config.
        createDeleteXClusterConfigSubtasks(
            xClusterConfig,
            true /* keepEntry */,
            taskParams().isForced(),
            false /* deletePitrConfigs */);

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

        createXClusterConfigSetStatusForTablesTask(
            xClusterConfig,
            getTableIds(requestedTableInfoList),
            XClusterTableConfig.Status.Updating);

        // Recreate the config including the new tables.
        addSubtasksToCreateXClusterConfig(
            xClusterConfig,
            requestedTableInfoList,
            mainTableIndexTablesMap,
            taskParams().getSourceTableIdsWithNoTableOnTargetUniverse(),
            taskParams().getPitrParams());
      } else {
        createRemoveTableFromXClusterConfigSubtasks(
            xClusterConfig, tableIdsDeleteReplication, true /* keepEntry */);

        if (xClusterConfig.isUsedForDr()) {
          createSetDrStatesTask(
                  xClusterConfig,
                  State.Initializing,
                  SourceUniverseState.Unconfigured,
                  TargetUniverseState.Unconfigured,
                  null /* keyspacePending */)
              .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);
        }

        createXClusterConfigSetStatusForTablesTask(
            xClusterConfig, tableIdsDeleteReplication, XClusterTableConfig.Status.Updating);

        // Add the subtasks to set up replication for tables that need bootstrapping.
        addSubtasksForTablesNeedBootstrap(
            xClusterConfig,
            taskParams().getBootstrapParams(),
            dbToTablesInfoMapNeedBootstrap,
            null /* sourceDbIds */,
            true /* isReplicationConfigCreated */,
            taskParams().getPitrParams());

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
      }
    }
  }

  private Set<String> getTableIdsToDeleteInReplicationDueToBootstrap(
      XClusterConfig xClusterConfig,
      Map<String, List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo>>
          dbToTablesInfoMapNeedBootstrap,
      Map<String, List<String>> mainTableIndexTablesMap,
      Set<String> tableIdsScheduledForBeingRemoved) {
    Set<String> tableIdsDeleteReplication = new HashSet<>();
    dbToTablesInfoMapNeedBootstrap.forEach(
        (namespaceName, tablesInfo) -> {
          Set<String> tableIdsNeedBootstrap = getTableIds(tablesInfo);
          if (taskParams().getBootstrapParams().allowBootstrap && !tablesInfo.isEmpty()) {
            if (tablesInfo.get(0).getTableType() == CommonTypes.TableType.PGSQL_TABLE_TYPE) {
              // For YSQL, Add all tables of the namespace if any table of namespace
              // requires bootstrap.
              List<TableInfo> sourceTablesInfo =
                  getTableInfoListByNamespaceName(
                      ybService,
                      Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID()),
                      CommonTypes.TableType.PGSQL_TABLE_TYPE,
                      namespaceName);
              tableIdsNeedBootstrap.addAll(getTableIds(sourceTablesInfo));
            } else {
              // For YCQL, Add all index tables and main table if any table requires
              // bootstrap.
              tablesInfo.forEach(
                  tableInfo -> {
                    String tableId = getTableId(tableInfo);
                    tableIdsNeedBootstrap.add(tableId);
                    if (TableInfoUtil.isIndexTable(tableInfo)) {
                      Optional<String> mainTableId =
                          getMainTableIdFromIndexTable(tableId, mainTableIndexTablesMap);
                      if (mainTableId.isPresent()) {
                        List<String> indexTableIds = mainTableIndexTablesMap.get(mainTableId.get());
                        tableIdsNeedBootstrap.add(mainTableId.get());
                        if (!CollectionUtils.isEmpty(indexTableIds)) {
                          tableIdsNeedBootstrap.addAll(indexTableIds);
                        }
                      }
                    }
                    List<String> indexTableIds = mainTableIndexTablesMap.get(tableId);
                    if (!CollectionUtils.isEmpty(indexTableIds)) {
                      tableIdsNeedBootstrap.addAll(indexTableIds);
                    }
                  });
            }
          }
          // YSQL tables replication with bootstrapping can only be set up with DB granularity.
          // The
          // following subtasks remove tables in replication, so the replication can be set up
          // again
          // for all the tables in the DB including the new tables.
          Set<String> tableIdsNeedBootstrapInReplication =
              xClusterConfig.getTableIdsWithReplicationSetup(
                  tableIdsNeedBootstrap, true /* done */);
          tableIdsDeleteReplication.addAll(
              tableIdsNeedBootstrapInReplication.stream()
                  .filter(tableId -> !tableIdsScheduledForBeingRemoved.contains(tableId))
                  .collect(Collectors.toSet()));
        });

    Universe sourceUniverse = Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID());
    Set<String> tableIdsWithReplicationSetup = xClusterConfig.getTableIdsWithReplicationSetup();
    // Since tableIdsDeleteReplication is a subset of the source universe tables that require
    // bootstrapping, it does not include tables that have been dropped from the source universe.
    // Additionally, when editing tables, we always aim to remove tables that have been
    // dropped from the source universe.
    Set<String> tableIdsWithReplicationSetupDroppedFromSource =
        getDroppedTableIds(this.ybService, sourceUniverse, tableIdsWithReplicationSetup);
    tableIdsDeleteReplication.addAll(tableIdsWithReplicationSetupDroppedFromSource);

    return tableIdsDeleteReplication;
  }

  protected void addSubtasksToAddDatabasesToXClusterConfig(
      XClusterConfig xClusterConfig, Set<String> databases) {

    addSubtasksForTablesNeedBootstrap(
        xClusterConfig,
        taskParams().getBootstrapParams(),
        null,
        databases,
        true /* isReplicationConfigCreated */,
        taskParams().getPitrParams());

    if (xClusterConfig.isUsedForDr()) {
      createSetDrStatesTask(
              xClusterConfig,
              State.Replicating,
              SourceUniverseState.ReplicatingData,
              TargetUniverseState.ReceivingData,
              null /* keyspacePending */)
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);
    }
  }

  protected void addSubtasksToRemoveDatabasesFromXClusterConfig(
      XClusterConfig xClusterConfig, Set<String> databases) {

    for (String dbId : databases) {
      createXClusterRemoveNamespaceFromTargetUniverseTask(xClusterConfig, dbId);
      createXClusterRemoveNamespaceFromOutboundReplicationGroupTask(xClusterConfig, dbId);
    }

    if (xClusterConfig.isUsedForDr()) {
      createSetDrStatesTask(
              xClusterConfig,
              State.Replicating,
              SourceUniverseState.ReplicatingData,
              TargetUniverseState.ReceivingData,
              null /* keyspacePending */)
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);
    }
  }

  private Optional<String> getMainTableIdFromIndexTable(
      String indexTableId, Map<String, List<String>> mainTableIndexTablesMap) {
    for (Map.Entry<String, List<String>> entry : mainTableIndexTablesMap.entrySet()) {
      if (entry.getValue().contains(indexTableId)) {
        return Optional.of(entry.getKey());
      }
    }
    return Optional.empty();
  }
}
