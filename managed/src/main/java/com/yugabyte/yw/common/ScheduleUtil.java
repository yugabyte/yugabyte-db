// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.helpers.TaskType;
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
}
