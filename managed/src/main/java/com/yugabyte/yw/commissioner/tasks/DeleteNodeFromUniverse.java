// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks;
import com.yugabyte.yw.commissioner.TaskListQueue;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.models.Universe;
import play.libs.Json;

public class DeleteNodeFromUniverse extends UniverseTaskBase {

  @Override
  protected NodeTaskParams taskParams() {
    return (NodeTaskParams)taskParams;
  }

  @Override
  public void run() {
    try {
      // Create the task list sequence.
      taskListQueue = new TaskListQueue(userTaskUUID);
      // Update the universe DB with the update to be performed and set the 'updateInProgress' flag
      // to prevent other updates from happening.
      Universe universe = lockUniverseForUpdate(taskParams().expectedUniverseVersion);
      LOG.info("Delete Node with name {} from universe {}", taskParams().nodeName, taskParams().universeUUID);
      deleteNodeFromUniverseTask(taskParams().nodeName)
        .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.DeletingNode);
      // Set Universe Update Success to true, if delete node succeeds for now.
      // Should probably roll back to a previous success state instead of setting to true
      createMarkUniverseUpdateSuccessTasks()
        .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.DeletingNode);
      taskListQueue.run();
    } catch (Throwable t) {
      LOG.error("Error executing task {}, error='{}'", getName(), t.getMessage(), t);
      throw t;
    } finally {
      unlockUniverseForUpdate();
    }
    LOG.info("Finished {} task.", getName());
  }
}
