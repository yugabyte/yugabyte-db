// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.ScheduleUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class DisableYbc extends UniverseTaskBase {

  @Inject
  protected DisableYbc(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  protected UniverseTaskParams taskParams() {
    return (UniverseTaskParams) taskParams;
  }

  @Override
  public void run() {
    try {
      // Update the universe DB with the update to be performed and set the 'updateInProgress' flag
      // to prevent other updates from happening. Does not alter 'updateSucceeded' flag so as not
      // to lock out the universe completely in case this task fails.
      lockUniverse(-1 /* expectedUniverseVersion */);

      Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
      UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();

      if (Schedule.getAllActiveSchedulesByOwnerUUIDAndType(
              universe.getUniverseUUID(), TaskType.CreateBackup)
          .stream()
          .anyMatch(s -> ScheduleUtil.isIncrementalBackupSchedule(s.getScheduleUUID()))) {
        throw new RuntimeException(
            "Cannot disable ybc as an incremental backup schedule exists on universe "
                + universe.getUniverseUUID());
      }

      if (!universeDetails.isEnableYbc() || !universeDetails.isYbcInstalled()) {
        throw new RuntimeException(
            "Ybc is either not installed or enabled on the universe: "
                + universe.getUniverseUUID());
      }
      // Stop yb-controller processes on nodes
      createStopYbControllerTasks(universe.getNodes())
          .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);

      // Update yb-controller state in universe details
      createDisableYbcUniverseDetails().setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      getRunnableTask().runSubTasks();
    } catch (Throwable t) {
      log.error("Error executing task {}, error='{}'", getName(), t.getMessage(), t);
      throw t;
    } finally {
      unlockUniverseForUpdate();
    }
    log.info("Finished {} task.", getName());
  }
}
