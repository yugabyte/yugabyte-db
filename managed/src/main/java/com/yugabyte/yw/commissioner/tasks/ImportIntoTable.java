// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.forms.BulkImportParams;
import lombok.extern.slf4j.Slf4j;

import javax.inject.Inject;

@Slf4j
public class ImportIntoTable extends UniverseTaskBase {

  @Inject
  protected ImportIntoTable(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected BulkImportParams taskParams() {
    return (BulkImportParams) taskParams;
  }

  @Override
  public void run() {
    try {
      // Create the task list sequence.
      subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);

      // Update the universe DB with the update to be performed and set the 'updateInProgress' flag
      // to prevent other updates from happening.
      lockUniverse(-1 /* expectedUniverseVersion */);

      // Create the task to run the EMR bulk import job.
      createBulkImportTask(taskParams())
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ImportingData);

      // Marks the update of this universe as a success only if all the tasks before it succeeded.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

      // Run all the tasks.
      subTaskGroupQueue.run();
    } catch (Throwable t) {
      log.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);
      throw t;
    } finally {
      // Mark the update of the universe as done and successful. This will allow future edits and
      // updates to the universe to happen.
      unlockUniverseForUpdate();
    }
    log.info("Finished {} task.", getName());
  }
}
