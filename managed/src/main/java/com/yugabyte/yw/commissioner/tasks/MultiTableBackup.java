/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Universe;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.GetTableSchemaResponse;
import org.yb.client.ListTablesResponse;
import org.yb.client.YBClient;
import org.yb.master.Master.ListTablesResponsePB.TableInfo;
import org.yb.master.Master.RelationType;

import javax.inject.Inject;
import java.util.*;

import static com.yugabyte.yw.common.Util.getUUIDRepresentation;
import static org.yb.Common.TableType;

@Slf4j
public class MultiTableBackup extends UniverseTaskBase {

  @Inject
  protected MultiTableBackup(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends BackupTableParams {
    public UUID customerUUID;
    public List<UUID> tableUUIDList = new ArrayList<>();
  }

  public Params params() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    List<BackupTableParams> backupParamsList = new ArrayList<>();
    BackupTableParams tableBackupParams = new BackupTableParams();
    Set<String> tablesToBackup = new HashSet<>();
    try {
      checkUniverseVersion();
      subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);

      // Update the universe DB with the update to be performed and set the 'updateInProgress' flag
      // to prevent other updates from happening.
      lockUniverse(-1 /* expectedUniverseVersion */);

      Universe universe = Universe.getOrBadRequest(params().universeUUID);
      String masterAddresses = universe.getMasterAddresses(true);
      String certificate = universe.getCertificateNodetoNode();

      YBClient client = null;
      Set<UUID> tableSet = new HashSet<>(params().tableUUIDList);
      try {
        client = ybService.getClient(masterAddresses, certificate);
        // If user specified the list of tables, only get info for those tables.
        if (tableSet.size() != 0) {
          for (UUID tableUUID : tableSet) {
            GetTableSchemaResponse tableSchema =
                client.getTableSchemaByUUID(tableUUID.toString().replace("-", ""));
            // If table is not REDIS or YCQL, ignore.
            if (tableSchema.getTableType() == TableType.PGSQL_TABLE_TYPE
                || tableSchema.getTableType() != params().backupType
                || tableSchema.getTableType() == TableType.TRANSACTION_STATUS_TABLE_TYPE) {
              log.info("Skipping backup of table with UUID: " + tableUUID);
              continue;
            }
            if (params().transactionalBackup) {
              populateBackupParams(
                  tableBackupParams,
                  tableSchema.getTableType(),
                  tableSchema.getNamespace(),
                  tableSchema.getTableName(),
                  tableUUID);
            } else {
              BackupTableParams backupParams =
                  createBackupParams(
                      tableSchema.getTableType(),
                      tableSchema.getNamespace(),
                      tableSchema.getTableName(),
                      tableUUID);
              backupParamsList.add(backupParams);
            }
            log.info(
                "Queuing backup for table {}:{}",
                tableSchema.getNamespace(),
                tableSchema.getTableName());

            tablesToBackup.add(
                String.format("%s:%s", tableSchema.getNamespace(), tableSchema.getTableName()));
          }
        }
        // If user did not specify tables, that means we need to backup all tables.
        else {
          // AC: This API call does not work when retrieving YSQL tables in specified
          // namespace, so we have to filter explicitly below
          ListTablesResponse response = client.getTablesList(null, true, null);
          List<TableInfo> tableInfoList = response.getTableInfoList();
          HashMap<String, BackupTableParams> keyspaceMap = new HashMap<>();
          for (TableInfo table : tableInfoList) {
            TableType tableType = table.getTableType();
            String tableKeySpace = table.getNamespace().getName();
            String tableUUIDString = table.getId().toStringUtf8();
            UUID tableUUID = getUUIDRepresentation(tableUUIDString);
            // If table is not REDIS or YCQL, ignore.
            if (tableType != params().backupType
                || tableType == TableType.TRANSACTION_STATUS_TABLE_TYPE
                || table.getRelationType() == RelationType.INDEX_TABLE_RELATION
                || (params().getKeyspace() != null
                    && !params().getKeyspace().equals(tableKeySpace))) {
              log.info(
                  "Skipping keyspace/universe backup of table "
                      + tableUUID
                      + ". Expected keyspace is "
                      + params().getKeyspace()
                      + "; actual keyspace is "
                      + tableKeySpace);
              continue;
            }

            if (tableType == TableType.PGSQL_TABLE_TYPE
                && !keyspaceMap.containsKey(tableKeySpace)) {
              // YSQL keyspaces must have prefix in front
              if (params().getKeyspace() != null) {
                populateBackupParams(tableBackupParams, tableType, tableKeySpace);
              } else {
                // Backing up entire universe
                BackupTableParams backupParams = createBackupParams(tableType, tableKeySpace);
                backupParamsList.add(backupParams);
                keyspaceMap.put(tableKeySpace, backupParams);
              }
            } else if (tableType == TableType.YQL_TABLE_TYPE
                || tableType == TableType.REDIS_TABLE_TYPE) {
              if (params().transactionalBackup && params().getKeyspace() != null) {
                populateBackupParams(
                    tableBackupParams, tableType, tableKeySpace, table.getName(), tableUUID);
              } else if (params().transactionalBackup && params().getKeyspace() == null) {
                // Backing up universe as transaction
                if (keyspaceMap.containsKey(tableKeySpace)) {
                  BackupTableParams currentBackup = keyspaceMap.get(tableKeySpace);
                  populateBackupParams(
                      currentBackup, tableType, tableKeySpace, table.getName(), tableUUID);
                } else {
                  // Current keyspace not in list, add new BackupTableParams
                  BackupTableParams backupParams =
                      createBackupParams(tableType, tableKeySpace, table.getName(), tableUUID);
                  backupParamsList.add(backupParams);
                  keyspaceMap.put(tableKeySpace, backupParams);
                }
              } else {
                BackupTableParams backupParams =
                    createBackupParams(tableType, tableKeySpace, table.getName(), tableUUID);
                backupParamsList.add(backupParams);
              }
            } else {
              log.error(
                  "Unrecognized table type {} for {}:{}",
                  tableType,
                  tableKeySpace,
                  table.getName());
            }

            log.info("Queuing backup for table {}:{}", tableKeySpace, table.getName());

            tablesToBackup.add(String.format("%s:%s", tableKeySpace, table.getName()));
          }
        }
        ybService.closeClient(client, masterAddresses);
      } catch (Exception e) {
        log.error("Failed to get list of tables in universe " + params().universeUUID, e);
        ybService.closeClient(client, masterAddresses);
        unlockUniverseForUpdate();
        throw new RuntimeException(e);
      }

      updateBackupState(true);

      subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);

      log.info("Successfully started scheduled backup of tables.");
      if (params().getKeyspace() == null && params().tableUUIDList.size() == 0) {
        // Full universe backup, each table to be sequentially backed up
        tableBackupParams.backupList = backupParamsList;
        tableBackupParams.storageConfigUUID = params().storageConfigUUID;
        tableBackupParams.actionType = BackupTableParams.ActionType.CREATE;
        tableBackupParams.storageConfigUUID = params().storageConfigUUID;
        tableBackupParams.universeUUID = params().universeUUID;
        tableBackupParams.sse = params().sse;
        tableBackupParams.parallelism = params().parallelism;
        tableBackupParams.timeBeforeDelete = params().timeBeforeDelete;
        tableBackupParams.transactionalBackup = params().transactionalBackup;
        tableBackupParams.backupType = params().backupType;
        tableBackupParams.backup = Backup.create(params().customerUUID, tableBackupParams);

        for (BackupTableParams backupParams : backupParamsList) {
          createEncryptedUniverseKeyBackupTask(backupParams)
              .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.CreatingTableBackup);
        }
        createTableBackupTask(tableBackupParams)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.CreatingTableBackup);
      } else if (params().getKeyspace() != null
          && (params().backupType == TableType.PGSQL_TABLE_TYPE
              || (params().backupType == TableType.YQL_TABLE_TYPE
                  && params().transactionalBackup))) {
        tableBackupParams.backup = Backup.create(params().customerUUID, tableBackupParams);
        createEncryptedUniverseKeyBackupTask(tableBackupParams.backup.getBackupInfo())
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.CreatingTableBackup);
        createTableBackupTask(tableBackupParams)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.CreatingTableBackup);
      } else {
        for (BackupTableParams tableParams : backupParamsList) {
          tableParams.backup = Backup.create(params().customerUUID, tableParams);
          createEncryptedUniverseKeyBackupTask(tableParams)
              .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.CreatingTableBackup);
          createTableBackupTask(tableParams)
              .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.CreatingTableBackup);
        }
      }

      // Marks the update of this universe as a success only if all the tasks before it succeeded.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

      taskInfo = String.join(",", tablesToBackup);

      unlockUniverseForUpdate();

      subTaskGroupQueue.run();
    } catch (Throwable t) {
      log.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);

      // Run an unlock in case the task failed before getting to the unlock. It is okay if it
      // errors out.
      unlockUniverseForUpdate();
      throw t;
    } finally {
      updateBackupState(false);
    }
    log.info("Finished {} task.", getName());
  }

  // Helper method to update passed in reference object
  private void populateBackupParams(
      BackupTableParams backupParams,
      TableType backupType,
      String tableKeySpace,
      String tableName,
      UUID tableUUID) {

    backupParams.actionType = BackupTableParams.ActionType.CREATE;
    backupParams.storageConfigUUID = params().storageConfigUUID;
    backupParams.universeUUID = params().universeUUID;
    backupParams.sse = params().sse;
    backupParams.parallelism = params().parallelism;
    backupParams.timeBeforeDelete = params().timeBeforeDelete;
    backupParams.scheduleUUID = params().scheduleUUID;
    backupParams.setKeyspace(tableKeySpace);
    backupParams.backupType = backupType;
    backupParams.transactionalBackup = params().transactionalBackup;

    if (tableName != null && tableUUID != null) {
      if (backupParams.tableNameList == null) {
        backupParams.tableNameList = new ArrayList<>();
        backupParams.tableUUIDList = new ArrayList<>();
        if (backupParams.getTableName() != null && backupParams.tableUUID != null) {
          // Clear singular fields and add to lists
          backupParams.tableNameList.add(backupParams.getTableName());
          backupParams.tableUUIDList.add(backupParams.tableUUID);
          backupParams.setTableName(null);
          backupParams.tableUUID = null;
        }
      }
      backupParams.tableNameList.add(tableName);
      backupParams.tableUUIDList.add(tableUUID);
    }
  }

  private void populateBackupParams(
      BackupTableParams backupParams, TableType backupType, String tableKeySpace) {
    populateBackupParams(backupParams, backupType, tableKeySpace, null, null);
  }

  private BackupTableParams createBackupParams(TableType backupType, String tableKeySpace) {
    return createBackupParams(backupType, tableKeySpace, null, null);
  }

  private BackupTableParams createBackupParams(
      TableType backupType, String tableKeySpace, String tableName, UUID tableUUID) {
    BackupTableParams backupParams = new BackupTableParams();
    backupParams.setKeyspace(tableKeySpace);
    backupParams.backupType = backupType;
    if (tableUUID != null && tableName != null) {
      backupParams.tableUUID = tableUUID;
      backupParams.setTableName(tableName);
    }
    backupParams.actionType = BackupTableParams.ActionType.CREATE;
    backupParams.storageConfigUUID = params().storageConfigUUID;
    backupParams.universeUUID = params().universeUUID;
    backupParams.sse = params().sse;
    backupParams.parallelism = params().parallelism;
    backupParams.timeBeforeDelete = params().timeBeforeDelete;
    backupParams.scheduleUUID = params().scheduleUUID;
    return backupParams;
  }
}
