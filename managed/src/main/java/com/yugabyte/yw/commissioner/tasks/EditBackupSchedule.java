package com.yugabyte.yw.commissioner.tasks;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.models.Schedule;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

@Slf4j
public class EditBackupSchedule extends BackupScheduleBase {

  @Inject
  protected EditBackupSchedule(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  public void validateParams(boolean isFirstTry) {
    super.validateParams(isFirstTry);
    if (isFirstTry) {
      Schedule schedule =
          Schedule.getOrBadRequest(taskParams().customerUUID, taskParams().scheduleUUID);
      boolean isIncrementalBackupSchedule =
          Json.fromJson(schedule.getTaskParams(), BackupRequestParams.class)
                  .incrementalBackupFrequency
              > 0L;
      taskParams()
          .scheduleParams
          .validateScheduleEditParams(backupHelper, getUniverse(), isIncrementalBackupSchedule);
    }
  }

  @Override
  public void run() {
    addAllEditBackupScheduleTasks(
        getBackupScheduleUniverseSubtasks(
            getUniverse(), taskParams().scheduleParams, false /* isDelete */),
        taskParams().scheduleParams,
        taskParams().customerUUID,
        taskParams().scheduleUUID);
  }
}
