// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.operator.OperatorStatusUpdaterFactory;
import com.yugabyte.yw.models.Schedule;
import java.util.Optional;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class DeleteBackupScheduleKubernetes extends BackupScheduleBaseKubernetes {

  @Inject
  protected DeleteBackupScheduleKubernetes(
      BaseTaskDependencies baseTaskDependencies,
      OperatorStatusUpdaterFactory operatorStatusUpdaterFactory) {
    super(baseTaskDependencies, operatorStatusUpdaterFactory);
  }

  @Override
  public void validateParams(boolean isFirstTry) {
    // Need no validation if PIT was not enabled for schedule
    if (!taskParams().scheduleParams.enablePointInTimeRestore) {
      return;
    }
    Optional<Schedule> optionalSchedule =
        Schedule.maybeGet(taskParams().customerUUID, taskParams().scheduleUUID);
    if (!optionalSchedule.isPresent()) {
      log.info("Schedule {} already deleted!", taskParams().scheduleUUID);
      return;
    }
    super.validateParams(isFirstTry);
  }

  @Override
  public void run() {
    addAllDeleteBackupScheduleTasks(
        getBackupScheduleUniverseSubtasks(
            getUniverse(), taskParams().scheduleParams, true /* isDelete */),
        taskParams().scheduleParams,
        taskParams().customerUUID,
        taskParams().scheduleUUID);
  }
}
