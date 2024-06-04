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

import static com.yugabyte.yw.commissioner.tasks.BackupUniverse.BACKUP_ATTEMPT_COUNTER;
import static com.yugabyte.yw.commissioner.tasks.BackupUniverse.BACKUP_FAILURE_COUNTER;
import static com.yugabyte.yw.commissioner.tasks.BackupUniverse.BACKUP_SUCCESS_COUNTER;
import static com.yugabyte.yw.commissioner.tasks.BackupUniverse.SCHEDULED_BACKUP_ATTEMPT_COUNTER;
import static com.yugabyte.yw.commissioner.tasks.BackupUniverse.SCHEDULED_BACKUP_FAILURE_COUNTER;
import static com.yugabyte.yw.commissioner.tasks.BackupUniverse.SCHEDULED_BACKUP_SUCCESS_COUNTER;
import static com.yugabyte.yw.common.Util.SYSTEM_PLATFORM_DB;
import static com.yugabyte.yw.common.Util.getUUIDRepresentation;
import static com.yugabyte.yw.common.metrics.MetricService.buildMetricTemplate;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.api.client.util.Throwables;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.tasks.subtasks.InstallThirdPartySoftwareK8s;
import com.yugabyte.yw.common.metrics.MetricLabelsBuilder;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.BackupTableParams.ActionType;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.ScheduleTask;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import com.yugabyte.yw.models.helpers.TaskType;
import io.swagger.annotations.ApiModel;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.CommonTypes.TableType;
import org.yb.client.GetTableSchemaResponse;
import org.yb.client.ListTablesResponse;
import org.yb.client.YBClient;
import org.yb.master.MasterDdlOuterClass.ListTablesResponsePB.TableInfo;
import org.yb.master.MasterTypes.RelationType;
import play.libs.Json;

@Slf4j
@Abortable
@Retryable
public class MultiTableBackup extends UniverseTaskBase {

  @Inject
  protected MultiTableBackup(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @ApiModel(value = "MultiTableBackupParams", description = "Multi-table backup parameters")
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
    tableBackupParams.customerUuid = params().customerUUID;
    tableBackupParams.ignoreErrors = true;
    Set<String> tablesToBackup = new HashSet<>();
    Universe universe = Universe.getOrBadRequest(params().getUniverseUUID());
    CloudType cloudType = universe.getUniverseDetails().getPrimaryCluster().userIntent.providerType;
    MetricLabelsBuilder metricLabelsBuilder = MetricLabelsBuilder.create().appendSource(universe);
    BACKUP_ATTEMPT_COUNTER.labels(metricLabelsBuilder.getPrometheusValues()).inc();
    try {
      checkUniverseVersion();

      // Update the universe DB with the update to be performed and set the 'updateInProgress' flag
      // to prevent other updates from happening.
      universe = lockAndFreezeUniverseForUpdate(-1, null /* Txn callback */);

      try {
        String masterAddresses = universe.getMasterAddresses();
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
                  CommonUtils.logTableName(tableSchema.getTableName()));

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
                  && params().getKeyspace() == null
                  && SYSTEM_PLATFORM_DB.equals(tableKeySpace)) {
                log.info("Skipping " + SYSTEM_PLATFORM_DB + " database");
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
                    CommonUtils.logTableName(table.getName()));
              }

              log.info(
                  "Queuing backup for table {}:{}",
                  tableKeySpace,
                  CommonUtils.logTableName(table.getName()));

              tablesToBackup.add(String.format("%s:%s", tableKeySpace, table.getName()));
            }
          }
        } catch (Exception e) {
          log.error("Failed to get list of tables in universe " + params().getUniverseUUID(), e);
          unlockUniverseForUpdate();
          // Do not lose the actual exception thrown.
          Throwables.propagate(e);
        } finally {
          ybService.closeClient(client, masterAddresses);
        }

        if (tablesToBackup.isEmpty()) {
          throw new RuntimeException("Invalid Keyspace or no tables to backup");
        }

        // Clear previous subtasks if any.
        getRunnableTask().reset();

        if (cloudType != CloudType.kubernetes) {
          // Ansible Configure Task for copying xxhsum binaries from
          // third_party directory to the DB nodes.
          installThirdPartyPackagesTask(universe)
              .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.InstallingThirdPartySoftware);
        } else {
          installThirdPartyPackagesTaskK8s(
                  universe, InstallThirdPartySoftwareK8s.SoftwareUpgradeType.XXHSUM)
              .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.InstallingThirdPartySoftware);
        }

        if (params().alterLoadBalancer) {
          createLoadBalancerStateChangeTask(false)
              .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);
        }
        log.info("Successfully started scheduled backup of tables.");
        if (params().getKeyspace() == null && params().tableUUIDList.size() == 0) {
          // Full universe backup, each table to be sequentially backed up

          tableBackupParams.backupList = backupParamsList;
          tableBackupParams.storageConfigUUID = params().storageConfigUUID;
          tableBackupParams.actionType = BackupTableParams.ActionType.CREATE;
          tableBackupParams.scheduleUUID = params().scheduleUUID;
          tableBackupParams.setUniverseUUID(params().getUniverseUUID());
          tableBackupParams.sse = params().sse;
          tableBackupParams.parallelism = params().parallelism;
          tableBackupParams.timeBeforeDelete = params().timeBeforeDelete;
          tableBackupParams.transactionalBackup = params().transactionalBackup;
          tableBackupParams.backupType = params().backupType;
          tableBackupParams.isFullBackup = true;
          tableBackupParams.disableChecksum = params().disableChecksum;
          tableBackupParams.useTablespaces = params().useTablespaces;
          tableBackupParams.disableParallelism = params().disableParallelism;

          Backup backup = Backup.create(params().customerUUID, tableBackupParams);
          backup.setTaskUUID(getUserTaskUUID());
          backup.save();
          tableBackupParams.backupUuid = backup.getBackupUUID();
          log.info("Task id {} for the backup {}", backup.getTaskUUID(), backup.getBackupUUID());

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
          Backup backup = Backup.create(params().customerUUID, tableBackupParams);
          backup.setTaskUUID(getUserTaskUUID());
          backup.save();
          tableBackupParams.backupUuid = backup.getBackupUUID();
          log.info("Task id {} for the backup {}", backup.getTaskUUID(), backup.getBackupUUID());

          createEncryptedUniverseKeyBackupTask(backup.getBackupInfo())
              .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.CreatingTableBackup);
          createTableBackupTask(tableBackupParams)
              .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.CreatingTableBackup);
        } else {
          for (BackupTableParams tableParams : backupParamsList) {
            Backup backup = Backup.create(params().customerUUID, tableParams);
            backup.setTaskUUID(getUserTaskUUID());
            backup.save();
            tableParams.backupUuid = backup.getBackupUUID();
            tableParams.customerUuid = backup.getCustomerUUID();
            tableParams.disableChecksum = params().disableChecksum;
            tableBackupParams.useTablespaces = params().useTablespaces;
            tableBackupParams.disableParallelism = params().disableParallelism;
            log.info("Task id {} for the backup {}", backup.getTaskUUID(), backup.getBackupUUID());

            createEncryptedUniverseKeyBackupTask(tableParams)
                .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.CreatingTableBackup);
            createTableBackupTask(tableParams)
                .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.CreatingTableBackup);
          }
        }

        // Marks the update of this universe as a success only if all the tasks before it succeeded.
        if (params().alterLoadBalancer) {
          createLoadBalancerStateChangeTask(true)
              .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);
        }
        createMarkUniverseUpdateSuccessTasks()
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

        taskInfo = String.join(",", tablesToBackup);

        getRunnableTask().runSubTasks();

        if (params().actionType == ActionType.CREATE) {
          BACKUP_SUCCESS_COUNTER.labels(metricLabelsBuilder.getPrometheusValues()).inc();
          metricService.setOkStatusMetric(
              buildMetricTemplate(PlatformMetrics.CREATE_BACKUP_STATUS, universe));
        }
      } catch (Throwable t) {
        if (params().alterLoadBalancer) {
          // If the task failed, we don't want the loadbalancer to be
          // disabled, so we enable it again in case of errors.
          setTaskQueueAndRun(
              () ->
                  createLoadBalancerStateChangeTask(true)
                      .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse));
        }
        throw t;
      }
    } catch (Throwable t) {
      log.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);

      if (params().actionType == ActionType.CREATE) {
        BACKUP_FAILURE_COUNTER.labels(metricLabelsBuilder.getPrometheusValues()).inc();
        metricService.setFailureStatusMetric(
            buildMetricTemplate(PlatformMetrics.CREATE_BACKUP_STATUS, universe));
      }
      throw t;
    } finally {
      unlockUniverseForUpdate();
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
    backupParams.setUniverseUUID(params().getUniverseUUID());
    backupParams.sse = params().sse;
    backupParams.parallelism = params().parallelism;
    backupParams.timeBeforeDelete = params().timeBeforeDelete;
    backupParams.scheduleUUID = params().scheduleUUID;
    backupParams.setKeyspace(tableKeySpace);
    backupParams.backupType = backupType;
    backupParams.transactionalBackup = params().transactionalBackup;
    backupParams.disableChecksum = params().disableChecksum;
    backupParams.useTablespaces = params().useTablespaces;
    backupParams.disableParallelism = params().disableParallelism;

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
    backupParams.setUniverseUUID(params().getUniverseUUID());
    backupParams.sse = params().sse;
    backupParams.parallelism = params().parallelism;
    backupParams.timeBeforeDelete = params().timeBeforeDelete;
    backupParams.scheduleUUID = params().scheduleUUID;
    backupParams.disableChecksum = params().disableChecksum;
    backupParams.useTablespaces = params().useTablespaces;
    backupParams.disableParallelism = params().disableParallelism;
    return backupParams;
  }

  public void runScheduledBackup(
      Schedule schedule, Commissioner commissioner, boolean alreadyRunning) {
    UUID customerUUID = schedule.getCustomerUUID();
    Customer customer = Customer.get(customerUUID);
    JsonNode params = schedule.getTaskParams();
    MultiTableBackup.Params taskParams = Json.fromJson(params, MultiTableBackup.Params.class);
    taskParams.scheduleUUID = schedule.getScheduleUUID();
    Universe universe;
    try {
      universe = Universe.getOrBadRequest(taskParams.getUniverseUUID());
    } catch (Exception e) {
      schedule.stopSchedule();
      return;
    }
    MetricLabelsBuilder metricLabelsBuilder = MetricLabelsBuilder.create().appendSource(universe);
    SCHEDULED_BACKUP_ATTEMPT_COUNTER.labels(metricLabelsBuilder.getPrometheusValues()).inc();
    Map<String, String> config = universe.getConfig();
    boolean shouldTakeBackup =
        !universe.getUniverseDetails().universePaused
            && config.get(Universe.TAKE_BACKUPS).equals("true");
    if (alreadyRunning || !shouldTakeBackup || universe.getUniverseDetails().updateInProgress) {

      if (shouldTakeBackup) {
        schedule.updateBacklogStatus(true);
        log.debug("Schedule {} backlog status is set to true", schedule.getScheduleUUID());
        SCHEDULED_BACKUP_FAILURE_COUNTER.labels(metricLabelsBuilder.getPrometheusValues()).inc();
        metricService.setFailureStatusMetric(
            buildMetricTemplate(PlatformMetrics.SCHEDULE_BACKUP_STATUS, universe));
      }

      String stateLogMsg = CommonUtils.generateStateLogMsg(universe, alreadyRunning);
      log.warn(
          "Cannot run Backup task on universe {} due to the state {}",
          taskParams.getUniverseUUID().toString(),
          stateLogMsg);
      return;
    }
    UUID taskUUID = commissioner.submit(TaskType.MultiTableBackup, taskParams);
    ScheduleTask.create(taskUUID, schedule.getScheduleUUID());
    if (schedule.isBacklogStatus()) {
      schedule.updateBacklogStatus(false);
      log.debug("Schedule {} backlog status is set to false", schedule.getScheduleUUID());
    }
    log.info(
        "Submitted backup for universe: {}, task uuid = {}.",
        taskParams.getUniverseUUID(),
        taskUUID);
    CustomerTask.create(
        customer,
        taskParams.getUniverseUUID(),
        taskUUID,
        CustomerTask.TargetType.Backup,
        CustomerTask.TaskType.Create,
        universe.getName());
    log.info(
        "Saved task uuid {} in customer tasks table for universe {}:{}",
        taskUUID,
        taskParams.getUniverseUUID(),
        universe.getName());
    SCHEDULED_BACKUP_SUCCESS_COUNTER.labels(metricLabelsBuilder.getPrometheusValues()).inc();
    metricService.setOkStatusMetric(
        buildMetricTemplate(PlatformMetrics.SCHEDULE_BACKUP_STATUS, universe));
  }
}
