// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.RestoreBackupParams;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Backup.BackupCategory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import java.io.File;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
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

        if (xClusterConfig.status != XClusterConfigStatusType.Init) {
          throw new RuntimeException(
              String.format(
                  "XClusterConfig(%s) must be in `Init` state to create replication for",
                  xClusterConfig.uuid));
        }
        if (xClusterConfig.getTables().size() < 1) {
          throw new RuntimeException(
              "At least one table must be selected to set up replication for");
        }

        // Ensure all the tables requested for replication setup have the same type, and belong to
        // one keyspace. If table type is YSQL and bootstrap is required, all tables in the keyspace
        // must be selected.
        List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTablesInfoList =
            checkTables();
        // At least one element exists in requestedTablesInfoList.
        CommonTypes.TableType tableType = requestedTablesInfoList.get(0).getTableType();

        // Support mismatched TLS root certificates.
        Optional<File> sourceCertificate =
            getSourceCertificateIfNecessary(sourceUniverse, targetUniverse);
        sourceCertificate.ifPresent(
            cert ->
                createSetupSourceCertificateTask(
                    targetUniverse, xClusterConfig.getReplicationGroupName(), cert));

        Set<String> tableIdsNeedBootstrap =
            getTablesNeedBootstrap()
                .stream()
                .map(tableConfig -> tableConfig.tableId)
                .collect(Collectors.toSet());
        checkBootstrapRequired(tableIdsNeedBootstrap);

        // If at least one YSQL table needs bootstrap, it must be done for all tables in that
        // keyspace.
        if (tableType == CommonTypes.TableType.PGSQL_TABLE_TYPE
            && getTablesNeedBootstrap().size() > 0) {
          xClusterConfig.setNeedBootstrapForTables(
              xClusterConfig.getTables(), true /* needBootstrap */);
        }

        // Replication for tables that do NOT need bootstrapping.
        Set<String> tableIdsNotNeedBootstrap =
            getTablesNotNeedBootstrap()
                .stream()
                .map(tableConfig -> tableConfig.tableId)
                .collect(Collectors.toSet());
        if (tableIdsNotNeedBootstrap.size() > 0) {
          // Set up the replication config.
          createXClusterConfigSetupTask(tableIdsNotNeedBootstrap)
              .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);
        }

        // Add the subtasks to set up replication for tables that need bootstrapping.
        addSubtasksForTablesNeedBootstrap(targetUniverse, requestedTablesInfoList);

        createXClusterConfigSetStatusTask(XClusterConfigStatusType.Running)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

        createMarkUniverseUpdateSuccessTasks(targetUniverse.universeUUID)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

        createMarkUniverseUpdateSuccessTasks(sourceUniverse.universeUUID)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

        getRunnableTask().runSubTasks();
      } catch (Exception e) {
        log.error("{} hit error : {}", getName(), e.getMessage());
        throw new RuntimeException(e);
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

  private void addSubtasksForTablesNeedBootstrap(
      Universe targetUniverse,
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTablesInfoList) {
    XClusterConfig xClusterConfig = taskParams().xClusterConfig;
    CommonTypes.TableType tableType = requestedTablesInfoList.get(0).getTableType();
    String namespace = requestedTablesInfoList.get(0).getNamespace().getName();

    Set<String> tableIdsNeedBootstrap =
        getTablesNeedBootstrap()
            .stream()
            .map(tableConfig -> tableConfig.tableId)
            .collect(Collectors.toSet());
    if (tableIdsNeedBootstrap.size() > 0) {
      createXClusterConfigSetStatusTask(XClusterConfigStatusType.Updating)
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.BootstrappingProducer);

      // Create checkpoints for the tables.
      createBootstrapProducerTask(tableIdsNeedBootstrap)
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.BootstrappingProducer);

      // Backup from the source universe. Currently, it only supports backup of the whole
      // keyspace.
      BackupRequestParams backupRequestParams =
          getBackupRequestParams(tableIdsNeedBootstrap, requestedTablesInfoList);
      Backup backup =
          createAllBackupSubtasks(
              backupRequestParams, UserTaskDetails.SubTaskGroupType.CreatingBackup);
      // Assign the created backup UUID for the tables in the DB.
      xClusterConfig.setBackupForTables(tableIdsNeedBootstrap, backup);

      // If the table type is YCQL, delete the tables from the target universe, because if the
      // tables exist, the restore subtask will fail.
      if (tableType == CommonTypes.TableType.YQL_TABLE_TYPE) {
        List<String> tableNamesNeedBootstrap =
            requestedTablesInfoList
                .stream()
                .filter(
                    tableInfo -> tableIdsNeedBootstrap.contains(tableInfo.getId().toStringUtf8()))
                .map(MasterDdlOuterClass.ListTablesResponsePB.TableInfo::getName)
                .collect(Collectors.toList());
        List<String> tableNamesToDeleteOnTargetUniverse =
            getTableInfoList(targetUniverse)
                .stream()
                .filter(
                    tableInfo ->
                        tableNamesNeedBootstrap.contains(tableInfo.getName())
                            && tableInfo.getNamespace().getName().equals(namespace))
                .map(MasterDdlOuterClass.ListTablesResponsePB.TableInfo::getName)
                .collect(Collectors.toList());
        createDeleteTablesFromUniverseTask(
                targetUniverse.universeUUID,
                Collections.singletonMap(namespace, tableNamesToDeleteOnTargetUniverse))
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.RestoringBackup);
      }

      // Restore to the target universe.
      RestoreBackupParams restoreBackupParams = getRestoreBackupParams(backupRequestParams, backup);
      createAllRestoreSubtasks(
          restoreBackupParams,
          UserTaskDetails.SubTaskGroupType.RestoringBackup,
          backup.category.equals(BackupCategory.YB_CONTROLLER));
      // Set the restore time for the tables in the DB.
      createSetRestoreTimeTask(tableIdsNeedBootstrap)
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.RestoringBackup);

      if (getTablesNotNeedBootstrap().size() > 0) {
        // It means the xCluster config is already created for tables without bootstrap.
        // We need to add the bootstrapped tables to the created xCluster config.
        createXClusterConfigModifyTablesTask(tableIdsNeedBootstrap, null /* tableIdsToRemove */)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);
      } else {
        // Set up the replication config.
        createXClusterConfigSetupTask(tableIdsNeedBootstrap)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);
      }
    }
  }

  /**
   * It ensures that all requested tables exist on the source universe, they have the same type,
   * they are in the same keyspace. Also, if the table type is YSQL and bootstrap is required, it
   * ensures all the tables in a keyspace are selected because per-table backup/restore is not
   * supported for YSQL. In addition, it ensures none of YCQL tables are index tables.
   *
   * @return A list of {@link MasterDdlOuterClass.ListTablesResponsePB.TableInfo} containing table
   *     info of the tables requested to be in the xCluster config
   */
  private List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> checkTables() {
    XClusterConfig xClusterConfig = taskParams().xClusterConfig;
    Universe sourceUniverse = Universe.getOrBadRequest(xClusterConfig.sourceUniverseUUID);
    Set<String> tableIds = xClusterConfig.getTables();
    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> sourceTablesInfoList =
        getTableInfoList(sourceUniverse);
    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTablesInfoList =
        sourceTablesInfoList
            .stream()
            .filter(tableInfo -> tableIds.contains(tableInfo.getId().toStringUtf8()))
            .collect(Collectors.toList());
    Set<String> tableIdsNeedBootstrap =
        getTablesNeedBootstrap()
            .stream()
            .map(tableConfig -> tableConfig.tableId)
            .collect(Collectors.toSet());
    // All tables are found.
    if (requestedTablesInfoList.size() != tableIds.size()) {
      Set<String> foundTableIds =
          requestedTablesInfoList
              .stream()
              .map(tableInfo -> tableInfo.getId().toStringUtf8())
              .collect(Collectors.toSet());
      Set<String> missingTableIds =
          tableIds
              .stream()
              .filter(tableId -> !foundTableIds.contains(tableId))
              .collect(Collectors.toSet());
      throw new RuntimeException(
          String.format(
              "Some of the tables were not found on the source universe (%s): was %d, "
                  + "found %d, missing tables: %s",
              xClusterConfig.sourceUniverseUUID,
              tableIds.size(),
              requestedTablesInfoList.size(),
              missingTableIds));
    }
    // All tables have the same type.
    if (!requestedTablesInfoList
        .stream()
        .allMatch(
            tableInfo ->
                tableInfo.getTableType().equals(requestedTablesInfoList.get(0).getTableType()))) {
      throw new RuntimeException(
          "At least one table has a different type from others. "
              + "All tables in an xCluster config must have the same type. Please create separate "
              + "xCluster configs for different table types.");
    }
    // All tables belong to the same keyspace.
    Set<String> namespaces =
        requestedTablesInfoList
            .stream()
            .map(tableInfo -> tableInfo.getNamespace().getName())
            .collect(Collectors.toSet());
    if (namespaces.size() != 1) {
      throw new RuntimeException(
          String.format(
              "All the tables must belong to one keyspace. The requested tables belong to %s",
              namespaces));
    }
    CommonTypes.TableType tableType = requestedTablesInfoList.get(0).getTableType();
    MasterTypes.NamespaceIdentifierPB namespace = requestedTablesInfoList.get(0).getNamespace();
    // If table type is YSQL and bootstrap is required, all tables in the keyspace are selected.
    Set<String> tableIdsInKeyspace =
        sourceTablesInfoList
            .stream()
            .filter(tableInfo -> tableInfo.getNamespace().getId().equals(namespace.getId()))
            .map(tableInfo -> tableInfo.getId().toStringUtf8())
            .collect(Collectors.toSet());
    if (tableType == CommonTypes.TableType.PGSQL_TABLE_TYPE
        && tableIdsNeedBootstrap.size() > 0
        && !tableIdsInKeyspace.equals(tableIds)) {
      throw new RuntimeException(
          String.format(
              "For YSQL tables, all the tables in a keyspace must be selected: selected: %s, "
                  + "tables in the keyspace: %s",
              tableIds, tableIdsInKeyspace));
    }
    // Backup index table is not supported for YCQL.
    if (tableType == CommonTypes.TableType.YQL_TABLE_TYPE) {
      List<String> indexTablesIdWithBootstrapList =
          requestedTablesInfoList
              .stream()
              .filter(
                  tableInfo ->
                      tableInfo.hasRelationType()
                          && tableInfo.getRelationType()
                              == MasterTypes.RelationType.INDEX_TABLE_RELATION)
              .map(tableInfo -> tableInfo.getId().toStringUtf8())
              .filter(tableIdsNeedBootstrap::contains)
              .collect(Collectors.toList());
      if (indexTablesIdWithBootstrapList.size() > 0) {
        throw new RuntimeException(
            String.format(
                "Bootstrap is not supported for YCQL index tables, but %s are index tables",
                indexTablesIdWithBootstrapList));
      }
    }

    log.info("Table type is {}, namesapce is {}", tableType, namespace.getName());
    return requestedTablesInfoList;
  }

  private List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> getTableInfoList(
      Universe universe) {
    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tableInfoList;
    String universeMasterAddresses = universe.getMasterAddresses(true /* mastersQueryable */);
    String universeCertificate = universe.getCertificateNodetoNode();
    try (YBClient client = ybService.getClient(universeMasterAddresses, universeCertificate)) {
      ListTablesResponse listTablesResponse = client.getTablesList(null, true, null);
      tableInfoList = listTablesResponse.getTableInfoList();
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
    return tableInfoList;
  }

  private BackupRequestParams getBackupRequestParams(
      Set<String> tableIdsNeedBootstrap,
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tablesInfoList) {
    BackupRequestParams backupRequestParams;
    if (taskParams().createFormData.bootstrapParams.backupRequestParams != null) {
      backupRequestParams = taskParams().createFormData.bootstrapParams.backupRequestParams;
    } else {
      // In case the user does not pass the backup parameters, use the default values.
      backupRequestParams = new BackupRequestParams();
      backupRequestParams.customerUUID =
          Customer.get(
                  Universe.getOrBadRequest(getXClusterConfigFromTaskParams().sourceUniverseUUID)
                      .customerId)
              .uuid;
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
    backupRequestParams.universeUUID = taskParams().xClusterConfig.sourceUniverseUUID;
    backupRequestParams.backupType = tablesInfoList.get(0).getTableType();
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
    keyspaceTable.keyspace = tablesInfoList.get(0).getNamespace().getName();
    if (backupRequestParams.backupType != CommonTypes.TableType.PGSQL_TABLE_TYPE) {
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tablesNeedBootstrapInfoList =
          tablesInfoList
              .stream()
              .filter(tableInfo -> tableIdsNeedBootstrap.contains(tableInfo.getId().toStringUtf8()))
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

  private RestoreBackupParams getRestoreBackupParams(
      BackupRequestParams backupRequestParams, Backup backup) {
    RestoreBackupParams restoreTaskParams = new RestoreBackupParams();
    // For the following parameters the default values will be used:
    //    restoreTaskParams.alterLoadBalancer = true
    //    restoreTaskParams.restoreTimeStamp = null
    //    public String oldOwner = "yugabyte"
    //    public String newOwner = null
    // The following parameters are set. For others, the defaults are good.
    restoreTaskParams.customerUUID = backupRequestParams.customerUUID;
    restoreTaskParams.universeUUID = taskParams().xClusterConfig.targetUniverseUUID;
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
