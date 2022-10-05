// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.RestoreBackupParams;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Backup.BackupCategory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import java.io.File;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.concurrent.TimeUnit;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.CommonTypes;
import org.yb.client.ListTablesResponse;
import org.yb.client.YBClient;
import org.yb.master.MasterDdlOuterClass;
import org.yb.master.MasterTypes;

@Slf4j
public class CreateXClusterConfig extends XClusterConfigTaskBase {

  public static final long TIME_BEFORE_DELETE_BACKUP_MS = TimeUnit.DAYS.toMillis(1);

  @Inject
  protected CreateXClusterConfig(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();
    Universe sourceUniverse = Universe.getOrBadRequest(xClusterConfig.sourceUniverseUUID);
    Universe targetUniverse = Universe.getOrBadRequest(xClusterConfig.targetUniverseUUID);
    try {
      // Lock the source universe.
      lockUniverseForUpdate(sourceUniverse.universeUUID, sourceUniverse.version);
      try {
        // Lock the target universe.
        lockUniverseForUpdate(targetUniverse.universeUUID, targetUniverse.version);

        createXClusterConfigSetStatusTask(XClusterConfigStatusType.Updating);

        Set<String> tableIds = xClusterConfig.getTables();
        Map<String, List<String>> mainTableIndexTablesMap =
            getMainTableIndexTablesMap(sourceUniverse, tableIds);
        addIndexTables(tableIds, mainTableIndexTablesMap);

        List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableInfoList =
            getRequestedTableInfoList(tableIds, sourceUniverse, targetUniverse);
        xClusterConfig.setTableType(requestedTableInfoList);

        addSubtasksToCreateXClusterConfig(
            sourceUniverse, targetUniverse, requestedTableInfoList, mainTableIndexTablesMap);

        createXClusterConfigSetStatusTask(XClusterConfigStatusType.Running)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

        createMarkUniverseUpdateSuccessTasks(targetUniverse.universeUUID)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

        createMarkUniverseUpdateSuccessTasks(sourceUniverse.universeUUID)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

        getRunnableTask().runSubTasks();
      } finally {
        // Unlock the target universe.
        unlockUniverseForUpdate(targetUniverse.universeUUID);
      }
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      setXClusterConfigStatus(XClusterConfigStatusType.Failed);
      throw new RuntimeException(e);
    } finally {
      // Unlock the source universe.
      unlockUniverseForUpdate(sourceUniverse.universeUUID);
    }

    log.info("Completed {}", getName());
  }

  protected void addSubtasksToCreateXClusterConfig(
      Universe sourceUniverse,
      Universe targetUniverse,
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableInfoList,
      Map<String, List<String>> mainTableIndexTablesMap) {
    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();

    // Support mismatched TLS root certificates.
    Optional<File> sourceCertificate =
        getSourceCertificateIfNecessary(sourceUniverse, targetUniverse);
    sourceCertificate.ifPresent(
        cert ->
            createTransferXClusterCertsCopyTasks(
                targetUniverse.getNodes(),
                xClusterConfig.getReplicationGroupName(),
                cert,
                targetUniverse.getUniverseDetails().getSourceRootCertDirPath()));

    Map<String, List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo>>
        requestedNamespaceNameTablesInfoMapNeedBootstrap =
            getRequestedNamespaceNameTablesInfoMapNeedBootstrap(
                requestedTableInfoList, mainTableIndexTablesMap);

    // Replication for tables that do NOT need bootstrapping.
    Set<String> tableIdsNotNeedBootstrap =
        getTableIdsNotNeedBootstrap(getTableIds(requestedTableInfoList));
    if (!tableIdsNotNeedBootstrap.isEmpty()) {
      log.info(
          "Creating a subtask to set up replication without bootstrap for tables {}",
          tableIdsNotNeedBootstrap);

      // Set up the replication config.
      createXClusterConfigSetupTask(tableIdsNotNeedBootstrap)
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);
    }

    // Add the subtasks to set up replication for tables that need bootstrapping.
    addSubtasksForTablesNeedBootstrap(
        sourceUniverse,
        targetUniverse,
        taskParams().createFormData.bootstrapParams,
        requestedNamespaceNameTablesInfoMapNeedBootstrap,
        !tableIdsNotNeedBootstrap.isEmpty());
  }

  protected void addSubtasksForTablesNeedBootstrap(
      Universe sourceUniverse,
      Universe targetUniverse,
      XClusterConfigCreateFormData.BootstrapParams bootstrapParams,
      Map<String, List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo>>
          requestedNamespaceNameTablesInfoMapNeedBootstrap,
      boolean isReplicationConfigCreated) {
    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();

    for (String namespaceName : requestedNamespaceNameTablesInfoMapNeedBootstrap.keySet()) {
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tablesInfoListNeedBootstrap =
          requestedNamespaceNameTablesInfoMapNeedBootstrap.get(namespaceName);
      if (tablesInfoListNeedBootstrap.isEmpty()) {
        throw new RuntimeException(
            String.format(
                "tablesInfoListNeedBootstrap in namespaceName %s is empty", namespaceName));
      }
      CommonTypes.TableType tableType = tablesInfoListNeedBootstrap.get(0).getTableType();
      Set<String> tableIdsNeedBootstrap = getTableIds(tablesInfoListNeedBootstrap);
      log.info(
          "Creating subtasks to set up replication using bootstrap for tables {} in "
              + "keyspace {}",
          tableIdsNeedBootstrap,
          namespaceName);

      // Create checkpoints for the tables.
      createBootstrapProducerTask(tableIdsNeedBootstrap)
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.BootstrappingProducer);

      // Backup from the source universe.
      BackupRequestParams backupRequestParams =
          getBackupRequestParams(sourceUniverse, bootstrapParams, tablesInfoListNeedBootstrap);
      Backup backup =
          createAllBackupSubtasks(
              backupRequestParams, UserTaskDetails.SubTaskGroupType.CreatingBackup);

      // Assign the created backup UUID for the tables in the DB.
      xClusterConfig.setBackupForTables(tableIdsNeedBootstrap, backup);

      // If the table type is YCQL, delete the tables from the target universe, because if the
      // tables exist, the restore subtask will fail.
      if (tableType == CommonTypes.TableType.YQL_TABLE_TYPE) {
        List<String> tableNamesNeedBootstrap =
            tablesInfoListNeedBootstrap
                .stream()
                .filter(
                    tableInfo ->
                        tableInfo.getRelationType()
                            != MasterTypes.RelationType.INDEX_TABLE_RELATION)
                .map(MasterDdlOuterClass.ListTablesResponsePB.TableInfo::getName)
                .collect(Collectors.toList());
        List<String> tableNamesToDeleteOnTargetUniverse =
            getTableInfoList(targetUniverse)
                .stream()
                .filter(
                    tableInfo ->
                        tableNamesNeedBootstrap.contains(tableInfo.getName())
                            && tableInfo.getNamespace().getName().equals(namespaceName))
                .map(MasterDdlOuterClass.ListTablesResponsePB.TableInfo::getName)
                .collect(Collectors.toList());
        createDeleteTablesFromUniverseTask(
                targetUniverse.universeUUID,
                Collections.singletonMap(namespaceName, tableNamesToDeleteOnTargetUniverse))
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.RestoringBackup);
      }

      // Restore to the target universe.
      RestoreBackupParams restoreBackupParams =
          getRestoreBackupParams(targetUniverse, backupRequestParams, backup);
      createAllRestoreSubtasks(
          restoreBackupParams,
          UserTaskDetails.SubTaskGroupType.RestoringBackup,
          backup.category.equals(BackupCategory.YB_CONTROLLER));
      // Set the restore time for the tables in the DB.
      createSetRestoreTimeTask(tableIdsNeedBootstrap)
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.RestoringBackup);

      if (isReplicationConfigCreated) {
        // If the xCluster config is already created, add the bootstrapped tables to the created
        // xCluster config.
        createXClusterConfigModifyTablesTask(tableIdsNeedBootstrap, null /* tableIdsToRemove */)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);
      } else {
        // Set up the replication config.
        createXClusterConfigSetupTask(tableIdsNeedBootstrap)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);
        isReplicationConfigCreated = true;
      }
    }
  }

  protected Map<String, List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo>>
      getRequestedNamespaceNameTablesInfoMapNeedBootstrap(
          List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableInfoList,
          Map<String, List<String>> mainTableIndexTablesMap) {
    if (requestedTableInfoList.isEmpty()) {
      log.warn("requestedTablesInfoList is empty");
      return Collections.emptyMap();
    }
    // At least one entry exists in requestedNamespaceNameTablesInfoMap and each list in any entry
    // has at least one TableInfo object.
    CommonTypes.TableType tableType = requestedTableInfoList.get(0).getTableType();
    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();
    Set<String> requestedTableIds = getTableIds(requestedTableInfoList);

    checkBootstrapRequiredForReplicationSetup(getTableIdsNeedBootstrap(requestedTableIds));

    Set<String> tableIdsNeedBootstrap = getTableIdsNeedBootstrap(requestedTableIds);
    groupByNamespaceName(requestedTableInfoList)
        .forEach(
            (namespaceName, tablesInfoList) -> {
              Set<String> tableIdsInNamespace = getTableIds(tablesInfoList);
              // If at least one YSQL table needs bootstrap, it must be done for all tables in that
              // keyspace.
              if (tableType == CommonTypes.TableType.PGSQL_TABLE_TYPE
                  && !getTablesNeedBootstrap(tableIdsInNamespace).isEmpty()) {
                xClusterConfig.setNeedBootstrapForTables(
                    tableIdsInNamespace, true /* needBootstrap */);
              }
              // If a main table or an index table of a main table needs bootstrapping, it must be
              // done
              // for the main table and all of its index tables due to backup/restore restrictions.
              mainTableIndexTablesMap.forEach(
                  (mainTableId, indexTableIds) -> {
                    if (tableIdsNeedBootstrap.contains(mainTableId)
                        || indexTableIds.stream().anyMatch(tableIdsNeedBootstrap::contains)) {
                      xClusterConfig.setNeedBootstrapForTables(
                          Collections.singleton(mainTableId), true /* needBootstrap */);
                      xClusterConfig.setNeedBootstrapForTables(
                          indexTableIds, true /* needBootstrap */);
                    }
                  });
            });

    // Get tables requiring bootstrap again in case previous statement has made any changes.
    Set<String> tableIdsNeedBootstrapAfterChanges = getTableIdsNeedBootstrap(requestedTableIds);

    return groupByNamespaceName(
        requestedTableInfoList
            .stream()
            .filter(
                tableInfo ->
                    tableIdsNeedBootstrapAfterChanges.contains(tableInfo.getId().toStringUtf8()))
            .collect(Collectors.toList()));
  }

  /**
   * It returns the list of table info from a universe for a set of table ids. Moreover, it ensures
   * that all requested tables exist on the source and target universes, and they have the same
   * type. Also, if the table type is YSQL and bootstrap is required, it ensures all the tables in a
   * keyspace are selected because per-table backup/restore is not supported for YSQL.
   *
   * @param requestedTableIds The table ids to get the {@code TableInfo} for
   * @param sourceUniverse The universe to gather the {@code TableInfo}s from
   * @param targetUniverse The universe to check that matching tables exist on
   * @return A list of {@link MasterDdlOuterClass.ListTablesResponsePB.TableInfo} containing table
   *     info of the tables whose id is specified at {@code requestedTableIds}
   */
  protected List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> getRequestedTableInfoList(
      Set<String> requestedTableIds, Universe sourceUniverse, Universe targetUniverse) {
    // Ensure at least one table exists to verify.
    if (requestedTableIds.isEmpty()) {
      throw new IllegalArgumentException("requestedTableIds cannot be empty");
    }
    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> sourceTableInfoList =
        getTableInfoList(sourceUniverse);
    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableInfoList =
        sourceTableInfoList
            .stream()
            .filter(tableInfo -> requestedTableIds.contains(tableInfo.getId().toStringUtf8()))
            .collect(Collectors.toList());

    // All tables are found on the source universe.
    if (requestedTableInfoList.size() != requestedTableIds.size()) {
      Set<String> foundTableIds = getTableIds(requestedTableInfoList);
      Set<String> missingTableIds =
          requestedTableIds
              .stream()
              .filter(tableId -> !foundTableIds.contains(tableId))
              .collect(Collectors.toSet());
      throw new IllegalArgumentException(
          String.format(
              "Some of the tables were not found on the source universe (Please note that "
                  + "it might be an index table): was %d, found %d, missing tables: %s",
              requestedTableIds.size(), requestedTableInfoList.size(), missingTableIds));
    }

    CommonTypes.TableType tableType = requestedTableInfoList.get(0).getTableType();
    // All tables have the same type.
    if (!requestedTableInfoList
        .stream()
        .allMatch(tableInfo -> tableInfo.getTableType().equals(tableType))) {
      throw new IllegalArgumentException(
          "At least one table has a different type from others. "
              + "All tables in an xCluster config must have the same type. Please create separate "
              + "xCluster configs for different table types.");
    }

    // XCluster replication can be set up only for YCQL and YSQL tables.
    if (!tableType.equals(CommonTypes.TableType.YQL_TABLE_TYPE)
        && !tableType.equals(CommonTypes.TableType.PGSQL_TABLE_TYPE)) {
      throw new IllegalArgumentException(
          String.format(
              "XCluster replication can be set up only for YCQL and YSQL tables: "
                  + "type %s requested",
              tableType));
    }
    log.info("All the requested tables are found and they have a type of {}", tableType);

    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> targetTablesInfoList =
        getTableInfoList(targetUniverse);
    checkTablesExistOnTargetUniverse(requestedTableInfoList, targetTablesInfoList);

    // If table type is YSQL and bootstrap is requested, all tables in that keyspace are selected.
    if (tableType == CommonTypes.TableType.PGSQL_TABLE_TYPE) {
      groupByNamespaceId(requestedTableInfoList)
          .forEach(
              (namespaceId, tablesInfoList) -> {
                Set<String> selectedTableIdsInNamespace = getTableIds(tablesInfoList);
                Set<String> tableIdsNeedBootstrap =
                    getTableIdsNeedBootstrap(selectedTableIdsInNamespace);
                if (!tableIdsNeedBootstrap.isEmpty()) {
                  Set<String> tableIdsInNamespace =
                      sourceTableInfoList
                          .stream()
                          .filter(
                              tableInfo ->
                                  tableInfo
                                      .getNamespace()
                                      .getId()
                                      .toStringUtf8()
                                      .equals(namespaceId))
                          .map(tableInfo -> tableInfo.getId().toStringUtf8())
                          .collect(Collectors.toSet());
                  if (tableIdsInNamespace.size() != selectedTableIdsInNamespace.size()) {
                    throw new IllegalArgumentException(
                        String.format(
                            "For YSQL tables, all the tables in a keyspace must be selected: "
                                + "selected: %s, tables in the keyspace: %s",
                            selectedTableIdsInNamespace, tableIdsInNamespace));
                  }
                }
              });
    }

    return requestedTableInfoList;
  }

  protected Set<String> addIndexTables(
      Set<String> tableIds, Map<String, List<String>> mainTableIndexTablesMap) {
    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();
    Set<String> indexTableIdSet =
        mainTableIndexTablesMap.values().stream().flatMap(List::stream).collect(Collectors.toSet());
    if ((taskParams().createFormData != null && taskParams().createFormData.bootstrapParams == null)
        || (taskParams().editFormData != null
            && taskParams().editFormData.bootstrapParams == null)) {
      xClusterConfig.addTablesIfNotExist(indexTableIdSet);
    } else {
      xClusterConfig.addTablesIfNotExist(indexTableIdSet, indexTableIdSet);
    }
    xClusterConfig.setIndexTableForTables(indexTableIdSet, true /* indexTable */);
    tableIds.addAll(indexTableIdSet);
    return tableIds;
  }

  protected void checkTablesExistOnTargetUniverse(
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedSourceTablesInfoList,
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> targetTablesInfoList) {
    if (requestedSourceTablesInfoList.isEmpty()) {
      log.warn("requestedSourceTablesInfoList is empty");
      return;
    }
    Set<String> notFoundNamespaces = new HashSet<>();
    Set<String> notFoundTables = new HashSet<>();
    CommonTypes.TableType tableType = requestedSourceTablesInfoList.get(0).getTableType();
    // Namespace name for one table type is unique.
    Map<String, List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo>>
        targetNamespaceTablesInfoMap =
            targetTablesInfoList
                .stream()
                .filter(tableInfo -> tableInfo.getTableType().equals(tableType))
                .collect(Collectors.groupingBy(tableInfo -> tableInfo.getNamespace().getName()));
    requestedSourceTablesInfoList.forEach(
        sourceTableInfo -> {
          // Find tables with the same keyspace name.
          List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> targetTableInfoListInNamespace =
              targetNamespaceTablesInfoMap.get(sourceTableInfo.getNamespace().getName());
          if (targetTableInfoListInNamespace != null) {
            if (tableType == CommonTypes.TableType.PGSQL_TABLE_TYPE) {
              // Check schema name match in case of YSQL.
              List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> targetTableInfoListInSchema =
                  targetTableInfoListInNamespace
                      .stream()
                      .filter(
                          tableInfo ->
                              tableInfo.getPgschemaName().equals(sourceTableInfo.getPgschemaName()))
                      .collect(Collectors.toList());
              // Check the table with the same name exists on the target universe.
              if (targetTableInfoListInSchema
                  .stream()
                  .map(MasterDdlOuterClass.ListTablesResponsePB.TableInfo::getName)
                  .noneMatch(tableName -> sourceTableInfo.getName().equals(tableName))) {
                notFoundTables.add(sourceTableInfo.getName());
              }
            } else {
              // Check the table with the same name exists on the target universe.
              if (targetTableInfoListInNamespace
                  .stream()
                  .map(MasterDdlOuterClass.ListTablesResponsePB.TableInfo::getName)
                  .noneMatch(tableName -> sourceTableInfo.getName().equals(tableName))) {
                notFoundTables.add(sourceTableInfo.getName());
              }
            }
          } else {
            notFoundNamespaces.add(sourceTableInfo.getNamespace().getName());
          }
        });
    if (!notFoundNamespaces.isEmpty() || !notFoundTables.isEmpty()) {
      throw new IllegalStateException(
          String.format(
              "Not found namespaces on the target universe: %s, not found tables on the "
                  + "target universe: %s",
              notFoundNamespaces, notFoundTables));
    }
  }

  static BackupRequestParams getBackupRequestParams(
      Universe sourceUniverse,
      XClusterConfigCreateFormData.BootstrapParams bootstrapParams,
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tablesInfoListNeedBootstrap) {
    BackupRequestParams backupRequestParams;
    if (bootstrapParams.backupRequestParams != null) {
      backupRequestParams = new BackupRequestParams(bootstrapParams.backupRequestParams);
    } else {
      // In case the user does not pass the backup parameters, use the default values.
      backupRequestParams = new BackupRequestParams();
      // Use the last storage config used for a successful backup as the default one.
      Optional<Backup> latestCompletedBackupOptional =
          Backup.fetchLatestByState(backupRequestParams.customerUUID, Backup.BackupState.Completed);
      if (!latestCompletedBackupOptional.isPresent()) {
        throw new RuntimeException(
            "bootstrapParams in XClusterConfigCreateFormData is null, and storageConfigUUID "
                + "cannot be determined based on the latest successful backup");
      }
      backupRequestParams.storageConfigUUID = latestCompletedBackupOptional.get().storageConfigUUID;
      log.info(
          "storageConfigUUID {} will be used for bootstrapping",
          backupRequestParams.storageConfigUUID);
    }
    // These parameters are pre-set. Others either come from the user, or the defaults are good.
    backupRequestParams.universeUUID = sourceUniverse.universeUUID;
    backupRequestParams.customerUUID = Customer.get(sourceUniverse.customerId).uuid;
    backupRequestParams.backupType = tablesInfoListNeedBootstrap.get(0).getTableType();
    backupRequestParams.timeBeforeDelete = TIME_BEFORE_DELETE_BACKUP_MS;
    backupRequestParams.expiryTimeUnit = com.yugabyte.yw.models.helpers.TimeUnit.MILLISECONDS;
    // Set to true because it is a beta version of bootstrapping, and we need to debug it.
    backupRequestParams.enableVerboseLogs = true;
    // Ensure keyspaceTableList is not specified by the user.
    if (backupRequestParams.keyspaceTableList != null) {
      throw new RuntimeException(
          "backupRequestParams.keyspaceTableList must be null, table selection happens "
              + "automatically");
    }
    backupRequestParams.keyspaceTableList = new ArrayList<>();
    BackupRequestParams.KeyspaceTable keyspaceTable = new BackupRequestParams.KeyspaceTable();
    keyspaceTable.keyspace = tablesInfoListNeedBootstrap.get(0).getNamespace().getName();
    if (backupRequestParams.backupType != CommonTypes.TableType.PGSQL_TABLE_TYPE) {
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tablesNeedBootstrapInfoList =
          tablesInfoListNeedBootstrap
              .stream()
              .filter(
                  tableInfo ->
                      tableInfo.getRelationType() != MasterTypes.RelationType.INDEX_TABLE_RELATION)
              .collect(Collectors.toList());
      keyspaceTable.tableNameList =
          tablesNeedBootstrapInfoList
              .stream()
              .map(MasterDdlOuterClass.ListTablesResponsePB.TableInfo::getName)
              .collect(Collectors.toList());
      keyspaceTable.tableUUIDList =
          tablesNeedBootstrapInfoList
              .stream()
              .map(tableInfo -> Util.getUUIDRepresentation(tableInfo.getId().toStringUtf8()))
              .collect(Collectors.toList());
    }
    backupRequestParams.keyspaceTableList.add(keyspaceTable);
    return backupRequestParams;
  }

  static RestoreBackupParams getRestoreBackupParams(
      Universe targetUniverse, BackupRequestParams backupRequestParams, Backup backup) {
    RestoreBackupParams restoreTaskParams = new RestoreBackupParams();
    // For the following parameters the default values will be used:
    //    restoreTaskParams.alterLoadBalancer = true
    //    restoreTaskParams.restoreTimeStamp = null
    //    public String oldOwner = "yugabyte"
    //    public String newOwner = null
    // The following parameters are set. For others, the defaults are good.
    restoreTaskParams.customerUUID = backupRequestParams.customerUUID;
    restoreTaskParams.universeUUID = targetUniverse.universeUUID;
    restoreTaskParams.kmsConfigUUID = backupRequestParams.kmsConfigUUID;
    if (restoreTaskParams.kmsConfigUUID != null) {
      restoreTaskParams.actionType = RestoreBackupParams.ActionType.RESTORE;
    } else {
      restoreTaskParams.actionType = RestoreBackupParams.ActionType.RESTORE_KEYS;
    }
    restoreTaskParams.enableVerboseLogs = backupRequestParams.enableVerboseLogs;
    restoreTaskParams.storageConfigUUID = backupRequestParams.storageConfigUUID;
    restoreTaskParams.useTablespaces = backupRequestParams.useTablespaces;
    restoreTaskParams.parallelism = backupRequestParams.parallelism;
    restoreTaskParams.disableChecksum = backupRequestParams.disableChecksum;
    restoreTaskParams.category = backup.category;
    // Set storage info.
    restoreTaskParams.backupStorageInfoList = new ArrayList<>();
    RestoreBackupParams.BackupStorageInfo backupStorageInfo =
        new RestoreBackupParams.BackupStorageInfo();
    backupStorageInfo.backupType = backupRequestParams.backupType;
    List<BackupTableParams> backupList = backup.getBackupInfo().backupList;
    if (backupList == null) {
      throw new RuntimeException("backup.getBackupInfo().backupList must not be null");
    }
    if (backupList.size() != 1) {
      String errMsg =
          String.format(
              "backup.getBackupInfo().backupList must have exactly one element, had %d",
              backupList.size());
      throw new RuntimeException(errMsg);
    }
    backupStorageInfo.storageLocation = backupList.get(0).storageLocation;
    List<BackupRequestParams.KeyspaceTable> keyspaceTableList =
        backupRequestParams.keyspaceTableList;
    if (keyspaceTableList == null) {
      throw new RuntimeException("backupRequestParams.keyspaceTableList must not be null");
    }
    if (keyspaceTableList.size() != 1) {
      String errMsg =
          String.format(
              "backupRequestParams.keyspaceTableList must have exactly one element, had %d",
              keyspaceTableList.size());
      throw new RuntimeException(errMsg);
    }
    backupStorageInfo.keyspace = keyspaceTableList.get(0).keyspace;
    backupStorageInfo.sse = backupRequestParams.sse;
    restoreTaskParams.backupStorageInfoList.add(backupStorageInfo);

    return restoreTaskParams;
  }
}
