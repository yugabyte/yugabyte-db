// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.TaskListQueue;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.tasks.subtasks.DeleteTableFromUniverse;

public class DeleteTable extends UniverseTaskBase {

  @Override
  protected DeleteTableFromUniverse.Params taskParams() {
    return (DeleteTableFromUniverse.Params)taskParams;
  }

  @Override
  public void run() {
    try {
      // Create the task list sequence.
      taskListQueue = new TaskListQueue(userTaskUUID);

      // Update the universe DB with the update to be performed and set the 'updateInProgress' flag
      // to prevent other updates from happening. Does not alter 'updateSucceeded' flag so as not
      // to lock out the universe completely in case this task fails.
      lockUniverse(-1 /* expectedUniverseVersion */);

      // Delete table task
      createDeleteTableFromUniverseTask(taskParams())
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.DeletingTable);

      // Run all the tasks.
      taskListQueue.run();
    } catch (Throwable t) {
      LOG.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);
      throw t;
    } finally {
      // Mark the update of the universe as done. This will allow any other pending tasks against
      // the universe to execute.
      unlockUniverseForUpdate();
    }
    LOG.info("Finished {} task.", getName());
  }
}
