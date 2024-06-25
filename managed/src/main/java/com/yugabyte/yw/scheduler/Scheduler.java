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
    updateExpiredNextIncrementScheduledTaskTime();
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
              if (schedule.isRunningState()) {
                schedule.setRunningState(false);
                schedule.save();
                log.debug(
                    "Updated scheduler {} running state to false", schedule.getScheduleUUID());
              }
            });
  }

  /** Updates expired next expected task time of active schedules in case of platform restarts. */
  public void updateExpiredNextScheduledTaskTime() {
    Schedule.getAll()
        .forEach(
            (schedule) -> {
              if (schedule.getStatus().equals(Schedule.State.Active)
                  && (schedule.getNextScheduleTaskTime() == null
                      || Util.isTimeExpired(schedule.getNextScheduleTaskTime()))) {
                schedule.updateNextScheduleTaskTime(Schedule.nextExpectedTaskTime(null, schedule));
                if (ScheduleUtil.isIncrementalBackupSchedule(schedule.getScheduleUUID())) {
                  schedule.updateNextIncrementScheduleTaskTime(
                      ScheduleUtil.nextExpectedIncrementTaskTime(schedule));
                }
              }
            });
  }

  /**
   * Updates expired next expected task time of active increment schedules in case of platform
   * restarts.
   */
  public void updateExpiredNextIncrementScheduledTaskTime() {
    Schedule.getAll()
        .forEach(
            (schedule) -> {
              if (schedule.getStatus().equals(Schedule.State.Active)
                  && (ScheduleUtil.isIncrementalBackupSchedule(schedule.getScheduleUUID()))
                  && (schedule.getNextIncrementScheduleTaskTime() == null
                      || Util.isTimeExpired(schedule.getNextIncrementScheduleTaskTime()))) {
                schedule.updateNextIncrementScheduleTaskTime(
                    ScheduleUtil.nextExpectedIncrementTaskTime(schedule));
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
        boolean isIncrementalBackupSchedule =
            ScheduleUtil.isIncrementalBackupSchedule(schedule.getScheduleUUID());
        long frequency = schedule.getFrequency();
        String cronExpression = schedule.getCronExpression();
        Date expectedScheduleTaskTime = schedule.getNextScheduleTaskTime();
        Date expectedIncrementScheduleTaskTime = schedule.getNextIncrementScheduleTaskTime();
        boolean backlogStatus = schedule.isBacklogStatus();
        boolean incrementBacklogStatus = schedule.isIncrementBacklogStatus();
        if (cronExpression == null && frequency == 0) {
          log.error(
              "Scheduled task does not have a recurrence specified {}", schedule.getScheduleUUID());
          continue;
        }
        try {
          schedule.setRunningState(true);
          schedule.save();
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

          Date currentTime = new Date();

          boolean isExpectedScheduleTaskTimeExpired = false;
          if (expectedScheduleTaskTime != null) {
            isExpectedScheduleTaskTimeExpired =
                Util.isTimeExpired(expectedScheduleTaskTime, currentTime);
          }
          boolean isExpectedIncrementScheduleTaskTime = false;
          if (expectedIncrementScheduleTaskTime != null) {
            isExpectedIncrementScheduleTaskTime =
                Util.isTimeExpired(expectedIncrementScheduleTaskTime, currentTime);
          }

          // Update next scheduled task time if it is expired or null.
          if (expectedScheduleTaskTime == null || isExpectedScheduleTaskTimeExpired) {
            Date nextScheduleTaskTime =
                Schedule.nextExpectedTaskTime(expectedScheduleTaskTime, schedule);
            expectedScheduleTaskTime =
                expectedScheduleTaskTime == null ? nextScheduleTaskTime : expectedScheduleTaskTime;
            schedule.updateNextScheduleTaskTime(nextScheduleTaskTime);
            log.debug(
                "Updating expired next schedule task time for {} to {} for schedule {}",
                expectedScheduleTaskTime,
                nextScheduleTaskTime,
                schedule.getScheduleUUID());
          }

          // Update next increment scheduled task time if it is expired or null.
          if (isIncrementalBackupSchedule
              && (expectedIncrementScheduleTaskTime == null
                  || isExpectedIncrementScheduleTaskTime)) {
            Date nextIncrementScheduleTaskTime =
                ScheduleUtil.nextExpectedIncrementTaskTime(schedule, currentTime);
            expectedIncrementScheduleTaskTime =
                expectedIncrementScheduleTaskTime == null
                    ? nextIncrementScheduleTaskTime
                    : expectedIncrementScheduleTaskTime;
            schedule.updateNextIncrementScheduleTaskTime(nextIncrementScheduleTaskTime);
            log.debug(
                "Updating expired next incremental schedule task time for {} to {} for schedule {}",
                expectedIncrementScheduleTaskTime,
                nextIncrementScheduleTaskTime,
                schedule.getScheduleUUID());
          }

          if (backlogStatus || incrementBacklogStatus) {
            log.debug(
                "Scheduling a backup for schedule {} due to backlog status: {}, incremental backlog"
                    + " status {}",
                schedule.getScheduleUUID(),
                backlogStatus,
                incrementBacklogStatus);
          }

          boolean shouldRunTask = isExpectedScheduleTaskTimeExpired || backlogStatus;
          UUID baseBackupUUID = null;
          if (isIncrementalBackupSchedule) {
            // fetch last successful full backup for the schedule on which incremental
            // backup can be taken.
            baseBackupUUID = fetchBaseBackupUUIDfromLatestSuccessfulBackup(schedule);
            if (shouldRunTask || baseBackupUUID == null) {
              // We won't do incremental backups if a full backup is due since
              // full backups take priority but make sure to take an incremental backup
              // either when it's scheduled or to catch up on any backlog.
              if (baseBackupUUID == null) {
                // If a scheduled backup is already not in progress and avoid running full backup.
                if (!verifyScheduledBackupInProgress(schedule)) {
                  shouldRunTask = true;
                }
              }
              baseBackupUUID = null;
              log.debug("Scheduling a full backup for schedule {}", schedule.getScheduleUUID());
            } else if (isExpectedIncrementScheduleTaskTime || incrementBacklogStatus) {
              shouldRunTask = true;
              log.debug(
                  "Scheduling a incremental backup for schedule {}", schedule.getScheduleUUID());
            }
          }

          if (shouldRunTask) {
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
          log.error("Error running schedule {} ", schedule.getScheduleUUID(), pe);
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
          log.error("Error running schedule {} ", schedule.getScheduleUUID(), e);
        } finally {
          schedule.setRunningState(false);
          schedule.save();
        }
      }
    } catch (Exception e) {
      log.error("Error Running scheduler thread", e);
    }
  }

  private UUID fetchBaseBackupUUIDfromLatestSuccessfulBackup(Schedule schedule) {
    Backup backup =
        ScheduleUtil.fetchLatestSuccessfulBackupForSchedule(
            schedule.getCustomerUUID(), schedule.getScheduleUUID());
    return backup == null ? null : backup.getBaseBackupUUID();
  }

  private boolean verifyScheduledBackupInProgress(Schedule schedule) {
    Backup backup =
        ScheduleUtil.fetchInProgressBackupForSchedule(
            schedule.getCustomerUUID(), schedule.getScheduleUUID());
    return backup == null ? false : true;
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
    if (schedule.isBacklogStatus()) {
      schedule.updateBacklogStatus(false);
    }
    UUID taskUUID = commissioner.submit(TaskType.ExternalScript, taskParams);
    ScheduleTask.create(taskUUID, schedule.getScheduleUUID());
    CustomerTask.create(
        customer,
        universe.getUniverseUUID(),
        taskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.ExternalScript,
        universe.getName());
    log.info(
        "Submitted external script task with task uuid = {} for universe {}.",
        taskUUID,
        universe.getUniverseUUID());
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
            ? customer.getUniversesForProvider(providerUUID).stream()
                .map(universe -> universe.getUniverseUUID())
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

    if (schedule.isBacklogStatus()) {
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
        provider.getName());
    log.info(
        "Submitted create and rotate access key task with task uuid = {} "
            + "for provider uuid = {}.",
        taskUUID,
        providerUUID);
  }
}
