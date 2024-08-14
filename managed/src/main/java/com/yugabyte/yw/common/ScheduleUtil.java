// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.Util.LOG;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.yugabyte.yw.common.backuprestore.BackupUtil;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Backup.BackupState;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.ScheduleTask;
import com.yugabyte.yw.models.helpers.TaskType;
import java.time.Duration;
import java.util.Date;
import java.util.Objects;
import java.util.UUID;
import org.apache.commons.lang3.StringUtils;
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

  public static Backup fetchInProgressBackupForSchedule(UUID customerUUID, UUID scheduleUUID) {
    Schedule schedule = Schedule.getOrBadRequest(customerUUID, scheduleUUID);
    ScheduleTask scheduleTask = ScheduleTask.getLastTask(schedule.getScheduleUUID());
    if (scheduleTask == null) {
      return null;
    }
    return Backup.fetchAllBackupsByTaskUUID(scheduleTask.getTaskUUID()).stream()
        .filter(bkp -> bkp.getState().equals(BackupState.InProgress))
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

  // Get PIT retention required based on full backup frequency/cron. If
  // incremental backup frequency is non zero, use incremental backup frequency as
  // final value.
  public static Duration getBackupIntervalForPITRestore(BackupRequestParams params) {
    if (!params.enablePointInTimeRestore) {
      return Duration.ofSeconds(0);
    }
    long intervalMillis =
        StringUtils.isNotBlank(params.cronExpression)
            ? BackupUtil.getCronExpressionTimeInterval(params.cronExpression)
            : params.schedulingFrequency;
    if (params.incrementalBackupFrequency > 0L) {
      intervalMillis = params.incrementalBackupFrequency;
    }
    // Return in seconds
    return Duration.ofMillis(intervalMillis);
  }

  /**
   * Get new PIT retention based on existing schedules and the provided schedule params. The
   * retention is the max of all schedule frequencies. In case of schedule delete, we take care of
   * returning max value without considering the schedule to be deleted.
   *
   * @param universeUUID
   * @param scheduleParams
   * @param scheduleToBeDeleted
   * @param bufferHistoryRetention
   * @return 0 if no change required to the retention value.
   *     <li>Duration of -1 if required to unset after delete if there are no PIT backup schedules
   *         left.
   *     <li>New frequency for all other cases i.e. max of all schedules
   */
  public static Duration getFinalHistoryRetentionUniverseForPITRestore(
      UUID universeUUID,
      BackupRequestParams scheduleParams,
      boolean scheduleToBeDeleted,
      Duration bufferHistoryRetention) {
    Duration currentMaxFrequency =
        Schedule.getMaxBackupIntervalInUniverseForPITRestore(
            universeUUID, false /* includeIntermediate */);
    Duration scheduleFrequency = ScheduleUtil.getBackupIntervalForPITRestore(scheduleParams);

    if (currentMaxFrequency.compareTo(scheduleFrequency) > 0) {
      // If no change required return 0
      return Duration.ofSeconds(0);
    }
    if (scheduleToBeDeleted) {
      // If no PIT based schedules left, return default retention
      // Else return current max + buffer time
      return currentMaxFrequency.isZero()
          ? Duration.ofSeconds(-1)
          : currentMaxFrequency.plus(bufferHistoryRetention);
    }
    // Return new max + buffer time
    return scheduleFrequency.plus(bufferHistoryRetention);
  }

  public static void checkScheduleActionFromDeprecatedMethod(Schedule schedule) {
    try {
      ObjectMapper mapper = new ObjectMapper();
      BackupRequestParams params =
          mapper.treeToValue(schedule.getTaskParams(), BackupRequestParams.class);
      if (params.enablePointInTimeRestore) {
        throw new RuntimeException(
            "Cannot modify/delete Point In Time Recovery enabled schedule using this deprecated"
                + " API. Use the corresponding new API instead.");
      }
    } catch (JsonProcessingException e) {
      LOG.debug("Skipping Schedule param validation");
    }
  }
}
