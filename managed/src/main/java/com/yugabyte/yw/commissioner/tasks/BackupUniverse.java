// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.forms.BackupTableParams;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class BackupUniverse extends UniverseTaskBase {

  public static final Logger LOG = LoggerFactory.getLogger(BackupUniverse.class);

  @Override
  protected BackupTableParams taskParams() {
    return (BackupTableParams) taskParams;
  }

  @Override
  public void run() {
    try {
      // Create the task list sequence.
      subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);

      // Update the universe DB with the update to be performed and set the 'updateInProgress' flag
      // to prevent other updates from happening.
      lockUniverse(-1 /* expectedUniverseVersion */);

      UserTaskDetails.SubTaskGroupType groupType;
      if (taskParams().actionType == BackupTableParams.ActionType.CREATE) {
        groupType = UserTaskDetails.SubTaskGroupType.CreatingTableBackup;
      } else if (taskParams().actionType == BackupTableParams.ActionType.RESTORE) {
        groupType = UserTaskDetails.SubTaskGroupType.RestoringTableBackup;
      } else {
        throw new RuntimeException("Invalid backup action type: " + taskParams().actionType);
      }
      createTableBackupTask(taskParams()).setSubTaskGroupType(groupType);

      // Marks the update of this universe as a success only if all the tasks before it succeeded.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

      // Run all the tasks.
      subTaskGroupQueue.run();
    } catch (Throwable t) {
      LOG.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);
      throw t;
    } finally {
      // Mark the update of the universe as done and successful. This will allow future edits and
      // updates to the universe to happen.
      unlockUniverseForUpdate();
    }
    LOG.info("Finished {} task.", getName());
  }
}
