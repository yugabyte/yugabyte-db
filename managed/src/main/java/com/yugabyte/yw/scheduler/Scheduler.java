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

import akka.actor.ActorSystem;
import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.annotations.VisibleForTesting;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.BackupUniverse;
import com.yugabyte.yw.commissioner.tasks.CreateBackup;
import com.yugabyte.yw.commissioner.tasks.MultiTableBackup;
import com.yugabyte.yw.commissioner.tasks.subtasks.DeleteBackupYb;
import com.yugabyte.yw.commissioner.tasks.subtasks.RunExternalScript;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.ScheduleTask;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.Date;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;
import scala.concurrent.ExecutionContext;
import scala.concurrent.duration.Duration;

@Singleton
@Slf4j
public class Scheduler {

  private static final int YB_SCHEDULER_INTERVAL = Util.YB_SCHEDULER_INTERVAL;

  private final ActorSystem actorSystem;
  private final ExecutionContext executionContext;

  private final AtomicBoolean running = new AtomicBoolean(false);

  private final Commissioner commissioner;

  @Inject
  Scheduler(ActorSystem actorSystem, ExecutionContext executionContext, Commissioner commissioner) {
    this.actorSystem = actorSystem;
    this.executionContext = executionContext;
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
    this.actorSystem
        .scheduler()
        .schedule(
            Duration.create(0, TimeUnit.MINUTES), // initialDelay
            Duration.create(YB_SCHEDULER_INTERVAL, TimeUnit.MINUTES), // interval
            this::scheduleRunner,
            this.executionContext);
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
    if (!running.compareAndSet(false, true)) {
      log.info("Previous run of scheduler is still underway");
      return;
    }

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
                this.runCreateBackupTask(schedule, alreadyRunning);
                break;
              default:
                log.error(
                    "Cannot schedule task {} for scheduler {}",
                    taskType,
                    schedule.getScheduleUUID());
                break;
            }
          }
        } catch (Exception e) {
          log.error("Error running schedule {} ", schedule.scheduleUUID, e);
        } finally {
          schedule.setRunningState(false);
        }
      }
      Map<Customer, List<Backup>> expiredBackups = Backup.getExpiredBackups();
      expiredBackups.forEach(
          (customer, backups) -> {
            deleteExpiredBackupsForCustomer(customer, backups);
          });
    } catch (Exception e) {
      log.error("Error Running scheduler thread", e);
    } finally {
      running.set(false);
    }
  }

  private void deleteExpiredBackupsForCustomer(Customer customer, List<Backup> expiredBackups) {
    Map<UUID, List<Backup>> expiredBackupsPerSchedule = new HashMap<>();
    List<Backup> backupsToDelete = new ArrayList<>();
    expiredBackups.forEach(
        backup -> {
          UUID scheduleUUID = backup.getScheduleUUID();
          if (scheduleUUID == null) {
            backupsToDelete.add(backup);
          } else {
            if (!expiredBackupsPerSchedule.containsKey(scheduleUUID)) {
              expiredBackupsPerSchedule.put(scheduleUUID, new ArrayList<>());
            }
            expiredBackupsPerSchedule.get(scheduleUUID).add(backup);
          }
        });
    for (UUID scheduleUUID : expiredBackupsPerSchedule.keySet()) {
      backupsToDelete.addAll(
          getBackupsToDeleteForSchedule(
              customer.getUuid(), scheduleUUID, expiredBackupsPerSchedule.get(scheduleUUID)));
    }

    for (Backup backup : backupsToDelete) {
      this.runDeleteBackupTask(customer, backup);
    }
  }

  private List<Backup> getBackupsToDeleteForSchedule(
      UUID customerUUID, UUID scheduleUUID, List<Backup> expiredBackups) {
    List<Backup> backupsToDelete = new ArrayList<Backup>();
    int minNumBackupsToRetain = Util.MIN_NUM_BACKUPS_TO_RETAIN,
        totalBackupsCount = Backup.fetchAllBackupsByScheduleUUID(customerUUID, scheduleUUID).size();
    Schedule schedule = Schedule.getOrBadRequest(scheduleUUID);
    if (schedule.getTaskParams().has("minNumBackupsToRetain")) {
      minNumBackupsToRetain = schedule.getTaskParams().get("minNumBackupsToRetain").intValue();
    }

    int numBackupsToDelete =
        Math.min(expiredBackups.size(), Math.max(0, totalBackupsCount - minNumBackupsToRetain));
    Collections.sort(
        expiredBackups,
        new Comparator<Backup>() {
          @Override
          public int compare(Backup b1, Backup b2) {
            return b1.getCreateTime().compareTo(b2.getCreateTime());
          }
        });
    for (int i = 0; i < Math.min(numBackupsToDelete, expiredBackups.size()); i++) {
      backupsToDelete.add(expiredBackups.get(i));
    }
    return backupsToDelete;
  }

  private void runBackupTask(Schedule schedule, boolean alreadyRunning) {
    BackupUniverse backupUniverse = AbstractTaskBase.createTask(BackupUniverse.class);
    backupUniverse.runScheduledBackup(schedule, commissioner, alreadyRunning);
  }

  private void runMultiTableBackupsTask(Schedule schedule, boolean alreadyRunning) {
    MultiTableBackup multiTableBackup = AbstractTaskBase.createTask(MultiTableBackup.class);
    multiTableBackup.runScheduledBackup(schedule, commissioner, alreadyRunning);
  }

  private void runCreateBackupTask(Schedule schedule, boolean alreadyRunning) {
    CreateBackup createBackup = AbstractTaskBase.createTask(CreateBackup.class);
    createBackup.runScheduledBackup(schedule, commissioner, alreadyRunning);
  }

  private void runDeleteBackupTask(Customer customer, Backup backup) {
    if (Backup.IN_PROGRESS_STATES.contains(backup.state)) {
      log.warn("Cannot delete backup {} since it is in a progress state", backup.backupUUID);
      return;
    }
    DeleteBackupYb.Params taskParams = new DeleteBackupYb.Params();
    taskParams.customerUUID = customer.getUuid();
    taskParams.backupUUID = backup.backupUUID;
    UUID taskUUID = commissioner.submit(TaskType.DeleteBackupYb, taskParams);
    log.info("Submitted task to delete backup {}, task uuid = {}.", backup.backupUUID, taskUUID);
    CustomerTask.create(
        customer,
        backup.backupUUID,
        taskUUID,
        CustomerTask.TargetType.Backup,
        CustomerTask.TaskType.Delete,
        "Backup");
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
          "Cannot run Backup task on universe {} due to the state {}",
          taskParams.universeUUID.toString(),
          stateLogMsg);
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
}
