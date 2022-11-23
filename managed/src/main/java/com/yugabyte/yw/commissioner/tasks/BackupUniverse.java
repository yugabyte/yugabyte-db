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

import static com.yugabyte.yw.common.metrics.MetricService.buildMetricTemplate;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.common.metrics.MetricLabelsBuilder;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.BackupTableParams.ActionType;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.KmsConfig;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.ScheduleTask;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import com.yugabyte.yw.models.helpers.TaskType;
import io.prometheus.client.CollectorRegistry;
import io.prometheus.client.Counter;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

@Slf4j
@Abortable
public class BackupUniverse extends UniverseTaskBase {

  // Counter names
  private static final String SCHEDULED_BACKUP_ATTEMPT_COUNT = "ybp_scheduled_backup_attempt";
  private static final String SCHEDULED_BACKUP_SUCCESS_COUNT = "ybp_scheduled_backup_success";
  private static final String SCHEDULED_BACKUP_FAILURE_COUNT = "ybp_scheduled_backup_failure";
  private static final String BACKUP_ATTEMPT_COUNT = "ybp_backup_attempt";
  private static final String BACKUP_SUCCESS_COUNT = "ybp_backup_success";
  private static final String BACKUP_FAILURE_COUNT = "ybp_backup_failure";

  // Counters
  public static final Counter SCHEDULED_BACKUP_ATTEMPT_COUNTER =
      Counter.build(
              SCHEDULED_BACKUP_ATTEMPT_COUNT, "Count of backup schedule attempts per universe")
          .labelNames(MetricLabelsBuilder.UNIVERSE_LABELS)
          .register(CollectorRegistry.defaultRegistry);
  public static final Counter SCHEDULED_BACKUP_SUCCESS_COUNTER =
      Counter.build(
              SCHEDULED_BACKUP_SUCCESS_COUNT,
              "Count of successful backup schedule attempts per universe")
          .labelNames(MetricLabelsBuilder.UNIVERSE_LABELS)
          .register(CollectorRegistry.defaultRegistry);
  public static final Counter SCHEDULED_BACKUP_FAILURE_COUNTER =
      Counter.build(
              SCHEDULED_BACKUP_FAILURE_COUNT,
              "Count of failed backup schedule attempts per universe")
          .labelNames(MetricLabelsBuilder.UNIVERSE_LABELS)
          .register(CollectorRegistry.defaultRegistry);
  public static final Counter BACKUP_ATTEMPT_COUNTER =
      Counter.build(BACKUP_ATTEMPT_COUNT, "Count of backup task attempts per universe")
          .labelNames(MetricLabelsBuilder.UNIVERSE_LABELS)
          .register(CollectorRegistry.defaultRegistry);
  public static final Counter BACKUP_SUCCESS_COUNTER =
      Counter.build(BACKUP_SUCCESS_COUNT, "Count of successful backup tasks per universe")
          .labelNames(MetricLabelsBuilder.UNIVERSE_LABELS)
          .register(CollectorRegistry.defaultRegistry);
  public static final Counter BACKUP_FAILURE_COUNTER =
      Counter.build(BACKUP_FAILURE_COUNT, "Count of failed backup tasks per universe")
          .labelNames(MetricLabelsBuilder.UNIVERSE_LABELS)
          .register(CollectorRegistry.defaultRegistry);

  @Inject
  protected BackupUniverse(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected BackupTableParams taskParams() {
    return (BackupTableParams) taskParams;
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().universeUUID);
    CloudType cloudType = universe.getUniverseDetails().getPrimaryCluster().userIntent.providerType;
    MetricLabelsBuilder metricLabelsBuilder = MetricLabelsBuilder.create().appendSource(universe);

    BACKUP_ATTEMPT_COUNTER.labels(metricLabelsBuilder.getPrometheusValues()).inc();
    try {
      checkUniverseVersion();
      // Update the universe DB with the update to be performed and set the 'updateInProgress' flag
      // to prevent other updates from happening.
      lockUniverse(-1 /* expectedUniverseVersion */);

      // Update universe 'backupInProgress' flag to true or throw an exception if universe is
      // already having a backup in progress.
      if (taskParams().actionType == BackupTableParams.ActionType.CREATE) {
        lockedUpdateBackupState(true);
      } else {
        // Check if the backup is in progress while other backup operations.
        if (universe.getUniverseDetails().backupInProgress) {
          throw new RuntimeException("A backup for this universe is already in progress.");
        }
      }
      if (taskParams().alterLoadBalancer) {
        createLoadBalancerStateChangeTask(false)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);
      }

      if (cloudType != CloudType.kubernetes) {
        // Ansible Configure Task for copying xxhsum binaries from
        // third_party directory to the DB nodes.
        installThirdPartyPackagesTask(universe)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.InstallingThirdPartySoftware);
      }

      try {
        UserTaskDetails.SubTaskGroupType groupType;
        if (taskParams().actionType == BackupTableParams.ActionType.CREATE) {
          groupType = UserTaskDetails.SubTaskGroupType.CreatingTableBackup;
          createEncryptedUniverseKeyBackupTask().setSubTaskGroupType(groupType);
          unlockUniverseForUpdate();
        } else if (taskParams().actionType == BackupTableParams.ActionType.RESTORE) {
          groupType = UserTaskDetails.SubTaskGroupType.RestoringTableBackup;

          // Handle case of backup being encrypted at rest
          if (KmsConfig.get(taskParams().kmsConfigUUID) != null) {
            // Download universe keys backup file for encryption at rest
            BackupTableParams restoreKeysParams = new BackupTableParams();
            restoreKeysParams.storageLocation = taskParams().storageLocation;
            restoreKeysParams.universeUUID = taskParams().universeUUID;
            restoreKeysParams.storageConfigUUID = taskParams().storageConfigUUID;
            restoreKeysParams.kmsConfigUUID = taskParams().kmsConfigUUID;
            restoreKeysParams.restoreTimeStamp = taskParams().restoreTimeStamp;
            restoreKeysParams.actionType = BackupTableParams.ActionType.RESTORE_KEYS;
            createTableBackupTask(restoreKeysParams).setSubTaskGroupType(groupType);

            // Restore universe keys backup file for encryption at rest
            createEncryptedUniverseKeyRestoreTask(taskParams()).setSubTaskGroupType(groupType);
          }
        } else {
          throw new RuntimeException("Invalid backup action type: " + taskParams().actionType);
        }

        createTableBackupTask(taskParams()).setSubTaskGroupType(groupType);

        Backup backup = Backup.create(taskParams().customerUuid, taskParams());
        backup.setTaskUUID(userTaskUUID);

        // Marks the update of this universe as a success only if all the tasks before it succeeded.
        if (taskParams().alterLoadBalancer) {
          createLoadBalancerStateChangeTask(true)
              .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);
        }
        createMarkUniverseUpdateSuccessTasks()
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

        Set<String> tableNames =
            taskParams()
                .getTableNames()
                .stream()
                .map(tableName -> taskParams().getKeyspace() + ":" + tableName)
                .collect(Collectors.toSet());

        taskInfo = String.join(",", tableNames);

        // Run all the tasks.
        getRunnableTask().runSubTasks();

        if (taskParams().actionType == ActionType.CREATE) {
          BACKUP_SUCCESS_COUNTER.labels(metricLabelsBuilder.getPrometheusValues()).inc();
          metricService.setOkStatusMetric(
              buildMetricTemplate(PlatformMetrics.CREATE_BACKUP_STATUS, universe));
        }
        if (taskParams().actionType != BackupTableParams.ActionType.CREATE) {
          unlockUniverseForUpdate();
        }
      } catch (Throwable t) {
        if (taskParams().alterLoadBalancer) {
          // Clear previous subtasks if any.
          getRunnableTask().reset();
          // If the task failed, we don't want the loadbalancer to be
          // disabled, so we enable it again in case of errors.
          createLoadBalancerStateChangeTask(true)
              .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);
          getRunnableTask().runSubTasks();
        }
        throw t;
      } finally {
        if (taskParams().actionType == BackupTableParams.ActionType.CREATE) {
          lockedUpdateBackupState(false);
        }
      }
    } catch (Throwable t) {
      try {
        log.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);
        if (taskParams().actionType == ActionType.CREATE) {
          BACKUP_FAILURE_COUNTER.labels(metricLabelsBuilder.getPrometheusValues()).inc();
          metricService.setFailureStatusMetric(
              buildMetricTemplate(PlatformMetrics.CREATE_BACKUP_STATUS, universe));
        }
      } finally {
        // Run an unlock in case the task failed before getting to the unlock. It is okay if it
        // errors out.
        unlockUniverseForUpdate();
      }
      throw t;
    }

    log.info("Finished {} task.", getName());
  }

  public void runScheduledBackup(
      Schedule schedule, Commissioner commissioner, boolean alreadyRunning) {
    UUID customerUUID = schedule.getCustomerUUID();
    Customer customer = Customer.get(customerUUID);
    JsonNode params = schedule.getTaskParams();
    BackupTableParams taskParams = Json.fromJson(params, BackupTableParams.class);
    taskParams.scheduleUUID = schedule.scheduleUUID;
    taskParams.customerUuid = customerUUID;
    Universe universe = Universe.maybeGet(taskParams.universeUUID).orElse(null);
    if (universe == null) {
      schedule.stopSchedule();
      return;
    }
    MetricLabelsBuilder metricLabelsBuilder = MetricLabelsBuilder.create().appendSource(universe);
    SCHEDULED_BACKUP_ATTEMPT_COUNTER.labels(metricLabelsBuilder.getPrometheusValues()).inc();
    if (alreadyRunning
        || universe.getUniverseDetails().backupInProgress
        || universe.getUniverseDetails().updateInProgress
        || universe.getUniverseDetails().universePaused) {
      if (!universe.getUniverseDetails().universePaused) {
        schedule.updateBacklogStatus(true);
        log.debug("Schedule {} backlog status is set to true", schedule.scheduleUUID);
        SCHEDULED_BACKUP_FAILURE_COUNTER.labels(metricLabelsBuilder.getPrometheusValues()).inc();
        metricService.setFailureStatusMetric(
            buildMetricTemplate(PlatformMetrics.SCHEDULE_BACKUP_STATUS, universe));
      }

      String stateLogMsg = CommonUtils.generateStateLogMsg(universe, alreadyRunning);
      log.warn(
          "Cannot run Backup task on universe {} due to the state {}",
          taskParams.universeUUID.toString(),
          stateLogMsg);
      return;
    }
    UUID taskUUID = commissioner.submit(TaskType.BackupUniverse, taskParams);
    ScheduleTask.create(taskUUID, schedule.getScheduleUUID());
    if (schedule.getBacklogStatus()) {
      schedule.updateBacklogStatus(false);
      log.debug("Schedule {} backlog status is set to false", schedule.scheduleUUID);
    }
    log.info(
        "Submitted task to backup table {}:{}, task uuid = {}.",
        taskParams.tableUUID,
        taskParams.getTableName(),
        taskUUID);
    CustomerTask.create(
        customer,
        taskParams.universeUUID,
        taskUUID,
        CustomerTask.TargetType.Backup,
        CustomerTask.TaskType.Create,
        taskParams.getTableName());
    log.info(
        "Saved task uuid {} in customer tasks table for table {}:{}.{}",
        taskUUID,
        taskParams.tableUUID,
        taskParams.getKeyspace(),
        taskParams.getTableName());
    SCHEDULED_BACKUP_SUCCESS_COUNTER.labels(metricLabelsBuilder.getPrometheusValues()).inc();
    metricService.setOkStatusMetric(
        buildMetricTemplate(PlatformMetrics.SCHEDULE_BACKUP_STATUS, universe));
  }
}
