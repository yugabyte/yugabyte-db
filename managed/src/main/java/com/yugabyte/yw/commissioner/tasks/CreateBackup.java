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
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.common.metrics.MetricLabelsBuilder;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Backup.BackupCategory;
import com.yugabyte.yw.models.Backup.BackupState;
import com.yugabyte.yw.models.Backup.BackupVersion;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.ScheduleTask;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.CustomerConfig.ConfigState;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.CancellationException;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections.CollectionUtils;
import org.yb.CommonTypes.TableType;
import org.yb.client.GetTableSchemaResponse;
import org.yb.client.ListTablesResponse;
import org.yb.client.YBClient;
import org.yb.master.MasterDdlOuterClass.ListTablesResponsePB.TableInfo;
import org.yb.master.MasterTypes.RelationType;
import play.libs.Json;

@Slf4j
@Abortable
public class CreateBackup extends UniverseTaskBase {

  @Inject
  protected CreateBackup(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  protected BackupRequestParams params() {
    return (BackupRequestParams) taskParams;
  }

  @Inject CustomerConfigService customerConfigService;

  @Override
  public void run() {
    Set<String> tablesToBackup = new HashSet<>();
    Universe universe = Universe.getOrBadRequest(params().universeUUID);
    MetricLabelsBuilder metricLabelsBuilder = MetricLabelsBuilder.create().appendSource(universe);
    BACKUP_ATTEMPT_COUNTER.labels(metricLabelsBuilder.getPrometheusValues()).inc();
    boolean isUniverseLocked = false;
    boolean ybcBackup =
        universe.isYbcEnabled() && !params().backupType.equals(TableType.REDIS_TABLE_TYPE);
    try {
      checkUniverseVersion();

      // Update the universe DB with the update to be performed and set the 'updateInProgress' flag
      // to prevent other updates from happening.
      lockUniverse(-1 /* expectedUniverseVersion */);
      isUniverseLocked = true;
      // Update universe 'backupInProgress' flag to true or throw an exception if universe is
      // already having a backup in progress.
      lockedUpdateBackupState(true);
      try {
        // Check if the storage config is in active state or not.
        CustomerConfig customerConfig =
            customerConfigService.getOrBadRequest(
                params().customerUUID, params().storageConfigUUID);
        if (!customerConfig.getState().equals(ConfigState.Active)) {
          throw new RuntimeException("Storage config cannot be used as it is not in Active state");
        }
        // Clear any previous subtasks if any.
        getRunnableTask().reset();

        Backup backup =
            createAllBackupSubtasks(
                params(),
                UserTaskDetails.SubTaskGroupType.CreatingTableBackup,
                tablesToBackup,
                ybcBackup);
        log.info("Task id {} for the backup {}", backup.taskUUID, backup.backupUUID);

        // Marks the update of this universe as a success only if all the tasks before it succeeded.
        createMarkUniverseUpdateSuccessTasks()
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

        taskInfo = String.join(",", tablesToBackup);

        unlockUniverseForUpdate();
        isUniverseLocked = false;
        getRunnableTask().runSubTasks();

        BACKUP_SUCCESS_COUNTER.labels(metricLabelsBuilder.getPrometheusValues()).inc();
        metricService.setOkStatusMetric(
            buildMetricTemplate(PlatformMetrics.CREATE_BACKUP_STATUS, universe));

      } catch (CancellationException ce) {
        log.error("Aborting backups for task: {}", userTaskUUID);
        Backup.fetchAllBackupsByTaskUUID(userTaskUUID)
            .forEach(backup -> backup.transitionState(BackupState.Stopped));
        throw ce;
      } catch (Throwable t) {
        if (params().alterLoadBalancer) {
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
        lockedUpdateBackupState(false);
      }
    } catch (Throwable t) {
      try {
        log.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);
        BACKUP_FAILURE_COUNTER.labels(metricLabelsBuilder.getPrometheusValues()).inc();
        metricService.setFailureStatusMetric(
            buildMetricTemplate(PlatformMetrics.CREATE_BACKUP_STATUS, universe));
      } finally {
        // Run an unlock in case the task failed before getting to the unlock. It is okay if it
        // errors out.
        if (isUniverseLocked) {
          unlockUniverseForUpdate();
        }
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
    BackupRequestParams taskParams = Json.fromJson(params, BackupRequestParams.class);
    taskParams.scheduleUUID = schedule.scheduleUUID;
    Universe universe;
    try {
      universe = Universe.getOrBadRequest(taskParams.universeUUID);
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
    if (alreadyRunning
        || !shouldTakeBackup
        || universe.getUniverseDetails().backupInProgress
        || universe.getUniverseDetails().updateInProgress) {
      if (shouldTakeBackup) {
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
    UUID taskUUID = commissioner.submit(TaskType.CreateBackup, taskParams);
    ScheduleTask.create(taskUUID, schedule.getScheduleUUID());
    if (schedule.getBacklogStatus()) {
      schedule.updateBacklogStatus(false);
      log.debug("Schedule {} backlog status is set to false", schedule.scheduleUUID);
    }
    log.info(
        "Submitted backup for universe: {}, task uuid = {}.", taskParams.universeUUID, taskUUID);
    CustomerTask.create(
        customer,
        taskParams.universeUUID,
        taskUUID,
        CustomerTask.TargetType.Backup,
        CustomerTask.TaskType.Create,
        universe.name);
    log.info(
        "Saved task uuid {} in customer tasks table for universe {}:{}",
        taskUUID,
        taskParams.universeUUID,
        universe.name);
    SCHEDULED_BACKUP_SUCCESS_COUNTER.labels(metricLabelsBuilder.getPrometheusValues()).inc();
    metricService.setOkStatusMetric(
        buildMetricTemplate(PlatformMetrics.SCHEDULE_BACKUP_STATUS, universe));
  }
}
