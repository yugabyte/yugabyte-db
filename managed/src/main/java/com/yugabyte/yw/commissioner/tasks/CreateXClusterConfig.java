// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.XClusterConfigModifyTables;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.RestoreBackupParams;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Backup.BackupCategory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.PitrConfig;
import com.yugabyte.yw.models.Restore;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.ConfigType;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import com.yugabyte.yw.models.XClusterTableConfig;
import java.io.File;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.TimeUnit;
import java.util.stream.Collectors;
import javax.annotation.Nullable;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.CommonTypes;
import org.yb.master.MasterDdlOuterClass;
import org.yb.master.MasterTypes;

@Slf4j
public class CreateXClusterConfig extends XClusterConfigTaskBase {

  public static final long TIME_BEFORE_DELETE_BACKUP_MS = TimeUnit.DAYS.toMillis(1);

  @Inject
  protected CreateXClusterConfig(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public List<Restore> restoreList = new ArrayList<>();

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

        createXClusterConfigSetStatusForTablesTask(
            getTableIds(taskParams().getTableInfoList(), taskParams().getTxnTableInfo()),
            XClusterTableConfig.Status.Updating);

        addSubtasksToCreateXClusterConfig(
            sourceUniverse,
            targetUniverse,
            taskParams().getTableInfoList(),
            taskParams().getMainTableIndexTablesMap(),
            taskParams().getTxnTableInfo());

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
      for (Restore restore : restoreList) {
        restore.update(taskUUID, Restore.State.Failed);
      }
      // Set tables in updating status to failed.
      Set<String> tablesInPendingStatus =
          xClusterConfig.getTableIdsInStatus(
              getTableIds(taskParams().getTableInfoList(), taskParams().getTxnTableInfo()),
              X_CLUSTER_TABLE_CONFIG_PENDING_STATUS_LIST);
      xClusterConfig.setStatusForTables(tablesInPendingStatus, XClusterTableConfig.Status.Failed);
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
      Map<String, List<String>> mainTableIndexTablesMap,
      @Nullable MasterDdlOuterClass.ListTablesResponsePB.TableInfo txnTableInfo) {
    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();

    // Support mismatched TLS root certificates.
    Optional<File> sourceCertificate =
        getSourceCertificateIfNecessary(sourceUniverse, targetUniverse);
    sourceCertificate.ifPresent(
        cert ->
            createTransferXClusterCertsCopyTasks(
                xClusterConfig,
                targetUniverse.getNodes(),
                xClusterConfig.getReplicationGroupName(),
                cert,
                targetUniverse.getUniverseDetails().getSourceRootCertDirPath()));

    Map<String, List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo>>
        dbToTablesInfoMapNeedBootstrap =
            getDbToTablesInfoMapNeedBootstrap(
                getTableIds(requestedTableInfoList),
                requestedTableInfoList,
                mainTableIndexTablesMap,
                txnTableInfo);

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
        taskParams().getBootstrapParams(),
        dbToTablesInfoMapNeedBootstrap,
        !tableIdsNotNeedBootstrap.isEmpty());
  }

  protected void addSubtasksForTablesNeedBootstrap(
      Universe sourceUniverse,
      Universe targetUniverse,
      XClusterConfigCreateFormData.BootstrapParams bootstrapParams,
      Map<String, List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo>>
          dbToTablesInfoMapNeedBootstrap,
      boolean isReplicationConfigCreated) {
    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();

    // Remove the txn InfoTable if it is present, and we are going to create an xCluster config.
    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> txnTableInfoList =
        dbToTablesInfoMapNeedBootstrap.remove(TRANSACTION_STATUS_TABLE_NAMESPACE);
    if (Objects.nonNull(txnTableInfoList)
        && !txnTableInfoList.isEmpty()
        && !isReplicationConfigCreated) {
      MasterDdlOuterClass.ListTablesResponsePB.TableInfo txnTableInfo = txnTableInfoList.get(0);
      createBootstrapProducerTask(getTableIds(Collections.singleton(txnTableInfo)))
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.BootstrappingProducer);
      log.info("Subtask to BootstrapProducer the txn table created");
    }

    for (String namespaceName : dbToTablesInfoMapNeedBootstrap.keySet()) {
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tablesInfoListNeedBootstrap =
          dbToTablesInfoMapNeedBootstrap.get(namespaceName);
      if (tablesInfoListNeedBootstrap.isEmpty()) {
        throw new RuntimeException(
            String.format(
                "tablesInfoListNeedBootstrap in namespaceName %s is empty", namespaceName));
      }
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

      CommonTypes.TableType tableType = tablesInfoListNeedBootstrap.get(0).getTableType();
      if (tableType == CommonTypes.TableType.YQL_TABLE_TYPE) {
        // If the table type is YCQL, delete the tables from the target universe, because if the
        // tables exist, the restore subtask will fail.
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
                        tableType.equals(tableInfo.getTableType())
                            && tableNamesNeedBootstrap.contains(tableInfo.getName())
                            && tableInfo.getNamespace().getName().equals(namespaceName))
                .map(MasterDdlOuterClass.ListTablesResponsePB.TableInfo::getName)
                .collect(Collectors.toList());
        createDeleteTablesFromUniverseTask(
                targetUniverse.universeUUID,
                Collections.singletonMap(namespaceName, tableNamesToDeleteOnTargetUniverse))
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.RestoringBackup);
      } else if (tableType == CommonTypes.TableType.PGSQL_TABLE_TYPE) {
        // If the table type is YSQL, delete the database from the target universe before restore.
        createDeleteKeySpaceTask(namespaceName, CommonTypes.TableType.PGSQL_TABLE_TYPE);
      }

      // Restore to the target universe.
      RestoreBackupParams restoreBackupParams =
          getRestoreBackupParams(sourceUniverse, targetUniverse, backupRequestParams, backup);
      Restore restore =
          createAllRestoreSubtasks(
              restoreBackupParams,
              UserTaskDetails.SubTaskGroupType.RestoringBackup,
              backup.category.equals(BackupCategory.YB_CONTROLLER));
      restoreList.add(restore);

      // Assign the created restore UUID for the tables in the DB.
      xClusterConfig.setRestoreForTables(tableIdsNeedBootstrap, restore);

      // Set the restore time for the tables in the DB.
      createSetRestoreTimeTask(tableIdsNeedBootstrap)
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.RestoringBackup);

      if (isReplicationConfigCreated) {
        // If the xCluster config is already created, add the bootstrapped tables to the created
        // xCluster config.
        createXClusterConfigModifyTablesTask(
                tableIdsNeedBootstrap, XClusterConfigModifyTables.Params.Action.ADD)
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
      getDbToTablesInfoMapNeedBootstrap(
          Set<String> tableIds,
          List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableInfoList,
          Map<String, List<String>> mainTableIndexTablesMap,
          @Nullable MasterDdlOuterClass.ListTablesResponsePB.TableInfo txnTableInfo) {
    if (requestedTableInfoList.isEmpty()) {
      log.warn("requestedTablesInfoList is empty");
      return Collections.emptyMap();
    }
    // At least one entry exists in requestedTableInfoList.
    CommonTypes.TableType tableType = requestedTableInfoList.get(0).getTableType();
    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();

    Set<String> tableIdsToCheckNeedBootstrap = getTableIdsNeedBootstrap(tableIds);
    if (Objects.nonNull(txnTableInfo)) {
      tableIdsToCheckNeedBootstrap.add(getTableId(txnTableInfo));
    }
    checkBootstrapRequiredForReplicationSetup(tableIdsToCheckNeedBootstrap);

    Set<String> tableIdsNeedBootstrap = getTableIdsNeedBootstrap(tableIds);
    Map<String, List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo>> dbToTableInfoListMap =
        groupByNamespaceName(requestedTableInfoList);
    dbToTableInfoListMap.forEach(
        (namespaceName, tablesInfoList) -> {
          Set<String> tableIdsInNamespace = getTableIds(tablesInfoList);
          // If at least one YSQL table needs bootstrap, it must be done for all tables in that
          // keyspace.
          if (tableType == CommonTypes.TableType.PGSQL_TABLE_TYPE
              && !getTablesNeedBootstrap(tableIdsInNamespace).isEmpty()) {
            xClusterConfig.setNeedBootstrapForTables(tableIdsInNamespace, true /* needBootstrap */);
          }
          // If a main table or an index table of a main table needs bootstrapping, it must be
          // done for the main table and all of its index tables due to backup/restore
          // restrictions.
          mainTableIndexTablesMap.forEach(
              (mainTableId, indexTableIds) -> {
                if (tableIdsNeedBootstrap.contains(mainTableId)
                    || indexTableIds.stream().anyMatch(tableIdsNeedBootstrap::contains)) {
                  xClusterConfig.setNeedBootstrapForTables(
                      Collections.singleton(mainTableId), true /* needBootstrap */);
                  xClusterConfig.setNeedBootstrapForTables(indexTableIds, true /* needBootstrap */);
                }
              });
        });

    // Get tables requiring bootstrap again in case previous statement has made any changes.
    Set<String> tableIdsNeedBootstrapAfterChanges =
        getTableIdsNeedBootstrap(getTableIds(requestedTableInfoList));

    Map<String, List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo>>
        dbToTablesInfoMapNeedBootstrap =
            requestedTableInfoList
                .stream()
                .filter(
                    tableInfo ->
                        tableIdsNeedBootstrapAfterChanges.contains(
                            tableInfo.getId().toStringUtf8()))
                .collect(Collectors.groupingBy(tableInfo -> tableInfo.getNamespace().getName()));

    if (Objects.nonNull(txnTableInfo)) {
      // Txn table needs bootstrapping if at least another table needs bootstrapping.
      if (!tableIdsNeedBootstrapAfterChanges.isEmpty()) {
        log.debug(
            "Setting txn table to be bootstrapping because there is at least one user table "
                + "that needs bootstrapping");
        xClusterConfig.setNeedBootstrapForTables(
            getTableIds(Collections.singleton(txnTableInfo)), true /* needBootstrap */);
      }
      if (xClusterConfig.getTxnTableDetails().needBootstrap) {
        // If txn needs bootstrapping, then all DBs needs bootstrapping because YBDB does not
        // support specifying bootstrap id for only txn table id.
        dbToTableInfoListMap.forEach(
            (namespace, tableInfoList) -> {
              xClusterConfig.setNeedBootstrapForTables(
                  getTableIds(tableInfoList), true /* needBootstrap */);
              dbToTablesInfoMapNeedBootstrap.put(namespace, tableInfoList);
            });
        log.info("txn table needs bootstrapping and thus it will bootstrap all DBs");
        dbToTablesInfoMapNeedBootstrap.put(
            txnTableInfo.getNamespace().getName(), Collections.singletonList(txnTableInfo));
        log.info("txn table added for bootstrapping");
      }
    }

    log.debug("dbToTablesInfoMapNeedBootstrap is {}", dbToTablesInfoMapNeedBootstrap);
    return dbToTablesInfoMapNeedBootstrap;
  }

  static BackupRequestParams getBackupRequestParams(
      Universe sourceUniverse,
      XClusterConfigCreateFormData.BootstrapParams bootstrapParams,
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tablesInfoListNeedBootstrap) {
    BackupRequestParams backupRequestParams;
    if (bootstrapParams != null && bootstrapParams.backupRequestParams != null) {
      backupRequestParams = new BackupRequestParams();
      backupRequestParams.storageConfigUUID = bootstrapParams.backupRequestParams.storageConfigUUID;
      backupRequestParams.parallelism = bootstrapParams.backupRequestParams.parallelism;
    } else {
      // In case the user does not pass the backup parameters, use the default values.
      backupRequestParams = new BackupRequestParams();
      backupRequestParams.customerUUID = Customer.get(sourceUniverse.customerId).uuid;
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
      Universe sourceUniverse,
      Universe targetUniverse,
      BackupRequestParams backupRequestParams,
      Backup backup) {
    RestoreBackupParams restoreTaskParams = new RestoreBackupParams();
    // For the following parameters the default values will be used:
    //    restoreTaskParams.alterLoadBalancer = true
    //    restoreTaskParams.restoreTimeStamp = null
    //    public String oldOwner = "yugabyte"
    //    public String newOwner = null
    // The following parameters are set. For others, the defaults are good.
    restoreTaskParams.customerUUID = backupRequestParams.customerUUID;
    restoreTaskParams.universeUUID = targetUniverse.universeUUID;
    if (sourceUniverse.getUniverseDetails().encryptionAtRestConfig != null) {
      restoreTaskParams.kmsConfigUUID =
          sourceUniverse.getUniverseDetails().encryptionAtRestConfig.kmsConfigUUID;
    }
    restoreTaskParams.actionType = RestoreBackupParams.ActionType.RESTORE;
    restoreTaskParams.enableVerboseLogs = backupRequestParams.enableVerboseLogs;
    restoreTaskParams.storageConfigUUID = backupRequestParams.storageConfigUUID;
    restoreTaskParams.useTablespaces = backupRequestParams.useTablespaces;
    restoreTaskParams.parallelism = backupRequestParams.parallelism;
    restoreTaskParams.disableChecksum = backupRequestParams.disableChecksum;
    restoreTaskParams.category = backup.category;
    restoreTaskParams.prefixUUID = UUID.randomUUID();
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
