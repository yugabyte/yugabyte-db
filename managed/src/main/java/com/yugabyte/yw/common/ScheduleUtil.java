// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Backup.BackupState;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.ScheduleTask;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.Date;
import java.util.Objects;
import java.util.UUID;
import play.libs.Json;

public class ScheduleUtil {

  /**
   * Checks if the provided backup schedule is a parent backup schedule which will take full backups
   * periodically on top of which incremental backups will be taken.
   */
  public static boolean isIncrementalBackupSchedule(UUID scheduleUUID) {
    Schedule schedule = Schedule.getOrBadRequest(scheduleUUID);
    try {
      if (!schedule.getTaskType().equals(TaskType.CreateBackup)) {
        return false;
      }
      BackupRequestParams scheduleParams =
          Json.fromJson(schedule.getTaskParams(), BackupRequestParams.class);
      return scheduleParams.incrementalBackupFrequency != 0L;
    } catch (Exception e) {
      return false;
    }
  }

  public static Backup fetchLatestSuccessfulBackupForSchedule(
      UUID customerUUID, UUID scheduleUUID) {
    Schedule schedule = Schedule.getOrBadRequest(customerUUID, scheduleUUID);
    ScheduleTask scheduleTask =
        ScheduleTask.getLastSuccessfulTask(schedule.getScheduleUUID()).orElse(null);
    if (scheduleTask == null) {
      return null;
    }
    return Backup.fetchAllBackupsByTaskUUID(scheduleTask.getTaskUUID()).stream()
        .filter(bkp -> bkp.getState().equals(BackupState.Completed))
        .findFirst()
        .orElse(null);
  }

  public static long getIncrementalBackupFrequency(Schedule schedule) {
    BackupRequestParams scheduleParams =
        Json.fromJson(schedule.getTaskParams(), BackupRequestParams.class);
    if (scheduleParams.incrementalBackupFrequency == 0L) {
      return 0L;
    }
    return scheduleParams.incrementalBackupFrequency;
  }

  public static Date nextExpectedIncrementTaskTime(Schedule schedule) {
    return nextExpectedIncrementTaskTime(schedule, new Date());
  }

  public static Date nextExpectedIncrementTaskTime(Schedule schedule, Date currentTime) {
    // Checking incremental backup frequency instead of using Util function as function is called
    // before object is created
    BackupRequestParams scheduleParams =
        Json.fromJson(schedule.getTaskParams(), BackupRequestParams.class);
    if (scheduleParams.incrementalBackupFrequency == 0L) {
      return null;
    }

    long incrementFrequency = scheduleParams.incrementalBackupFrequency;

    Date nextScheduleTaskTime = schedule.getNextScheduleTaskTime();
    if (Objects.isNull(nextScheduleTaskTime)) {
      long newIncrementScheduleTaskTime = (new Date()).getTime() + incrementFrequency;
      return new Date(newIncrementScheduleTaskTime);
    }

    Date nextIncrementScheduleTaskTime = schedule.getNextIncrementScheduleTaskTime();
    if (Objects.isNull(nextIncrementScheduleTaskTime)) {
      return new Date(nextScheduleTaskTime.getTime() + incrementFrequency);
    }

    // check if calculated increment backup time is after current time
    do {
      nextIncrementScheduleTaskTime =
          new Date(nextIncrementScheduleTaskTime.getTime() + incrementFrequency);
    } while (currentTime.after(nextIncrementScheduleTaskTime));

    return nextIncrementScheduleTaskTime;
  }
}
