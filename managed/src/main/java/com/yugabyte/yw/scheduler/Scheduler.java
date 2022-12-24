/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1
 * .0.0.txt
 */

package com.yugabyte.yw.scheduler;

import static play.mvc.Http.Status.SERVICE_UNAVAILABLE;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.annotations.VisibleForTesting;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.BackupUniverse;
import com.yugabyte.yw.commissioner.tasks.CreateBackup;
import com.yugabyte.yw.commissioner.tasks.MultiTableBackup;
import com.yugabyte.yw.commissioner.tasks.params.ScheduledAccessKeyRotateParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.RunExternalScript;
import com.yugabyte.yw.common.AccessKeyRotationUtil;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ScheduleUtil;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.ScheduleTask;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.TaskType;
import java.time.Duration;
import java.util.Date;
import java.util.List;
import java.util.UUID;
import java.util.concurrent.TimeUnit;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

@Singleton
@Slf4j
public class Scheduler {

  private static final int YB_SCHEDULER_INTERVAL = Util.YB_SCHEDULER_INTERVAL;
  private static final long RETRY_INTERVAL_SECONDS = 30;

  private final PlatformScheduler platformScheduler;

  private final Commissioner commissioner;

  @Inject AccessKeyRotationUtil accessKeyRotationUtil;

  @Inject
  Scheduler(PlatformScheduler platformScheduler, Commissioner commissioner) {
    this.platformScheduler = platformScheduler;
    this.commissioner = commissioner;
  }

  public void init() {
    resetRunningStatus();
    // Update next expected task for active schedule which got expired due to platform downtime.
    updateExpiredNextScheduledTaskTime();
    // start scheduler
    start();
  }

  public void start() {
    log.info("Starting scheduling service");
    platformScheduler.schedule(
        getClass().getSimpleName(),
        Duration.ZERO,
        Duration.ofMinutes(YB_SCHEDULER_INTERVAL),
        this::scheduleRunner);
  }

  /** Resets every schedule's running state to false in case of platform restart. */
  public void resetRunningStatus() {
    Schedule.getAll()
        .forEach(
            (schedule) -> {
              if (schedule.getRunningState()) {
                schedule.setRunningState(false);
                log.debug("Updated scheduler {} running state to false", schedule.scheduleUUID);
              }
            });
  }

  /** Updates expired next expected task time of active schedules in case of platform restarts. */
  public void updateExpiredNextScheduledTaskTime() {
    Schedule.getAll()
        .forEach(
            (schedule) -> {
              if (schedule.getStatus().equals(Schedule.State.Active)
                  && Util.isTimeExpired(schedule.getNextScheduleTaskTime())) {
                schedule.updateNextScheduleTaskTime(Schedule.nextExpectedTaskTime(null, schedule));
              }
            });
  }

  /** Iterates through all the schedule entries and runs the tasks that are due to be scheduled. */
  @VisibleForTesting
  void scheduleRunner() {
    try {
      if (HighAvailabilityConfig.isFollower()) {
        log.debug("Skipping scheduler for follower platform");
        return;
      }

      log.info("Running scheduler");
      for (Schedule schedule : Schedule.getAllActive()) {
        long frequency = schedule.getFrequency();
        String cronExpression = schedule.getCronExpression();
        Date expectedScheduleTaskTime = schedule.getNextScheduleTaskTime();
        boolean backlogStatus = schedule.getBacklogStatus();
        if (cronExpression == null && frequency == 0) {
          log.error(
              "Scheduled task does not have a recurrence specified {}", schedule.getScheduleUUID());
          continue;
        }
        try {
          schedule.setRunningState(true);
          TaskType taskType = schedule.getTaskType();
          ScheduleTask lastTask = ScheduleTask.getLastTask(schedule.getScheduleUUID());
          Date lastScheduledTime = null;
          Date lastCompletedTime = null;
          if (lastTask != null) {
            lastScheduledTime = lastTask.getScheduledTime();
            lastCompletedTime = lastTask.getCompletedTime();
          }

          // Check if the previous scheduled task is still running.
          boolean alreadyRunning = false;
          if (lastScheduledTime != null && lastCompletedTime == null) {
            alreadyRunning = true;
          }

          // Update expected scheduled time if it is expired or null.
          if (expectedScheduleTaskTime == null || Util.isTimeExpired(expectedScheduleTaskTime)) {
            Date nextScheduleTaskTime =
                Schedule.nextExpectedTaskTime(expectedScheduleTaskTime, schedule);
            expectedScheduleTaskTime =
                expectedScheduleTaskTime == null ? nextScheduleTaskTime : expectedScheduleTaskTime;
            schedule.updateNextScheduleTaskTime(nextScheduleTaskTime);
          }

          boolean shouldRunTask = Util.isTimeExpired(expectedScheduleTaskTime);
          UUID baseBackupUUID = null;
          if (!shouldRunTask && ScheduleUtil.isIncrementalBackupSchedule(schedule.scheduleUUID)) {
            baseBackupUUID = fetchBaseBackupUUIDIfIncrementalBackupRequired(schedule);
            if (baseBackupUUID != null) {
              shouldRunTask = true;
            }
          }

          if (shouldRunTask || backlogStatus) {
            switch (taskType) {
              case BackupUniverse:
                this.runBackupTask(schedule, alreadyRunning);
                break;
              case MultiTableBackup:
                this.runMultiTableBackupsTask(schedule, alreadyRunning);
                break;
              case ExternalScript:
                this.runExternalScriptTask(schedule, alreadyRunning);
                break;
              case CreateBackup:
                this.runCreateBackupTask(schedule, alreadyRunning, baseBackupUUID);
                break;
              case CreateAndRotateAccessKey:
                this.runAccessKeyRotation(schedule, alreadyRunning);
              default:
                log.error(
                    "Cannot schedule task {} for scheduler {}",
                    taskType,
                    schedule.getScheduleUUID());
                break;
            }
          }
        } catch (PlatformServiceException pe) {
          log.error("Error running schedule {} ", schedule.scheduleUUID, pe);
          if (pe.getHttpStatus() == SERVICE_UNAVAILABLE) {
            Date retryDate =
                new Date(
                    System.currentTimeMillis() + TimeUnit.SECONDS.toMillis(RETRY_INTERVAL_SECONDS));
            Date nextScheduleTime = schedule.getNextScheduleTaskTime();
            if (nextScheduleTime.after(retryDate)) {
              log.debug("Received 503, will retry at {}", retryDate);
              schedule.updateNextScheduleTaskTime(retryDate);
            }
          }
        } catch (Exception e) {
          log.error("Error running schedule {} ", schedule.scheduleUUID, e);
        } finally {
          schedule.setRunningState(false);
        }
      }
    } catch (Exception e) {
      log.error("Error Running scheduler thread", e);
    }
  }

  private UUID fetchBaseBackupUUIDIfIncrementalBackupRequired(Schedule schedule) {
    Backup backup =
        ScheduleUtil.fetchLatestSuccessfulBackupForSchedule(
            schedule.getCustomerUUID(), schedule.getScheduleUUID());
    if (backup == null) {
      return null;
    }
    BackupRequestParams params = Json.fromJson(schedule.getTaskParams(), BackupRequestParams.class);
    long incrementalBackupFrequency = params.incrementalBackupFrequency;
    Date todaysDate = new Date();
    Date expectedTaskExecutionTime =
        new Date(backup.getCreateTime().getTime() + incrementalBackupFrequency);
    if (todaysDate.after(expectedTaskExecutionTime)) {
      return backup.baseBackupUUID;
    }
    return null;
  }

  private void runBackupTask(Schedule schedule, boolean alreadyRunning) {
    BackupUniverse backupUniverse = AbstractTaskBase.createTask(BackupUniverse.class);
    backupUniverse.runScheduledBackup(schedule, commissioner, alreadyRunning);
  }

  private void runMultiTableBackupsTask(Schedule schedule, boolean alreadyRunning) {
    MultiTableBackup multiTableBackup = AbstractTaskBase.createTask(MultiTableBackup.class);
    multiTableBackup.runScheduledBackup(schedule, commissioner, alreadyRunning);
  }

  private void runCreateBackupTask(Schedule schedule, boolean alreadyRunning, UUID baseBackupUUID) {
    CreateBackup createBackup = AbstractTaskBase.createTask(CreateBackup.class);
    createBackup.runScheduledBackup(schedule, commissioner, alreadyRunning, baseBackupUUID);
  }

  private void runExternalScriptTask(Schedule schedule, boolean alreadyRunning) {
    JsonNode params = schedule.getTaskParams();
    RunExternalScript.Params taskParams = Json.fromJson(params, RunExternalScript.Params.class);
    Customer customer = Customer.getOrBadRequest(taskParams.customerUUID);
    Universe universe;
    try {
      universe = Universe.getOrBadRequest(taskParams.universeUUID);
    } catch (Exception e) {
      schedule.stopSchedule();
      log.info(
          "External script scheduler is stopped for the universe {} as universe was deleted.",
          taskParams.universeUUID);
      return;
    }
    if (alreadyRunning
        || universe.getUniverseDetails().updateInProgress
        || universe.getUniverseDetails().universePaused) {
      if (!alreadyRunning) {
        schedule.updateBacklogStatus(true);
      }
      String stateLogMsg = CommonUtils.generateStateLogMsg(universe, alreadyRunning);
      log.warn(
          "Cannot run External Script task on universe {} due to the state {}",
          taskParams.universeUUID.toString(),
          stateLogMsg);
      return;
    }
    if (schedule.getBacklogStatus()) {
      schedule.updateBacklogStatus(false);
    }
    UUID taskUUID = commissioner.submit(TaskType.ExternalScript, taskParams);
    ScheduleTask.create(taskUUID, schedule.getScheduleUUID());
    CustomerTask.create(
        customer,
        universe.universeUUID,
        taskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.ExternalScript,
        universe.name);
    log.info(
        "Submitted external script task with task uuid = {} for universe {}.",
        taskUUID,
        universe.universeUUID);
  }

  private void runAccessKeyRotation(Schedule schedule, boolean alreadyRunning) {
    JsonNode params = schedule.getTaskParams();
    ScheduledAccessKeyRotateParams taskParams =
        Json.fromJson(params, ScheduledAccessKeyRotateParams.class);
    UUID providerUUID = taskParams.getProviderUUID();
    UUID customerUUID = taskParams.getCustomerUUID();
    boolean rotateAllUniverses = taskParams.isRotateAllUniverses();
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Provider provider = Provider.getOrBadRequest(customerUUID, providerUUID);

    List<UUID> universeUUIDs =
        rotateAllUniverses
            ? customer
                .getUniversesForProvider(providerUUID)
                .stream()
                .map(universe -> universe.universeUUID)
                .collect(Collectors.toList())
            : taskParams.getUniverseUUIDs();

    // filter out deleted universes
    universeUUIDs = accessKeyRotationUtil.removeDeletedUniverses(universeUUIDs);
    if (universeUUIDs.size() == 0) {
      log.info(
          "Scheduled access key rotation is stopped for schedule {} "
              + "as all universes are deleted.",
          schedule.getScheduleUUID());
      schedule.stopSchedule();
      return;
    }

    if (alreadyRunning) {
      log.warn(
          "Did not run scheduled access key rotation for schedule {} "
              + "due to ongoing scheduled rotation task",
          schedule.getScheduleUUID());
      return;
    }

    if (schedule.getBacklogStatus()) {
      schedule.updateBacklogStatus(false);
    }

    taskParams.setUniverseUUIDs(universeUUIDs);
    UUID taskUUID = commissioner.submit(TaskType.CreateAndRotateAccessKey, taskParams);
    ScheduleTask.create(taskUUID, schedule.getScheduleUUID());
    CustomerTask.create(
        customer,
        providerUUID,
        taskUUID,
        CustomerTask.TargetType.Provider,
        CustomerTask.TaskType.CreateAndRotateAccessKey,
        provider.name);
    log.info(
        "Submitted create and rotate access key task with task uuid = {} "
            + "for provider uuid = {}.",
        taskUUID,
        providerUUID);
  }
}
