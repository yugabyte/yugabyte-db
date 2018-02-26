// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType;

import java.util.Arrays;
import java.util.HashSet;

public class StopNodeInUniverse extends UniverseTaskBase {

  @Override
  protected NodeTaskParams taskParams() {
    return (NodeTaskParams)taskParams;
  }

  @Override
  public void run() {
    try {
      // Create the task list sequence.
      subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);

      // Set the 'updateInProgress' flag to prevent other updates from happening.
      Universe universe = lockUniverseForUpdate(taskParams().expectedUniverseVersion);
      LOG.info("Stop Node with name {} from universe {}", taskParams().nodeName,
               taskParams().universeUUID);

      NodeDetails currentNode = universe.getNode(taskParams().nodeName);
      if (currentNode == null) {
        String msg = "No node " + taskParams().nodeName + " found in universe " + universe.name;
        LOG.error(msg);
        throw new RuntimeException(msg);
      }

      // Update Node State to Stopping
      createSetNodeStateTask(currentNode, NodeState.Stopping)
          .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);

      // Stop the tserver and remove its state from YW DB.
      createTServerTaskForNode(currentNode, "stop")
          .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
      createUpdateNodeProcessTask(taskParams().nodeName, ServerType.TSERVER, false)
          .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);

      // Stop the master process on this node and remove its state from YW DB.
      if (currentNode.isMaster) {
        createStopMasterTasks(new HashSet<NodeDetails>(Arrays.asList(currentNode)))
          .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
        createUpdateNodeProcessTask(taskParams().nodeName, ServerType.MASTER, false)
          .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
        createWaitForMasterLeaderTask()
          .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
      }

      // Update Node State to Stopped
      createSetNodeStateTask(currentNode, NodeState.Stopped)
          .setSubTaskGroupType(SubTaskGroupType.StoppingNode);

      // Mark universe task state to success
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.StoppingNode);

      subTaskGroupQueue.run();
    } catch (Throwable t) {
      LOG.error("Error executing task {}, error='{}'", getName(), t.getMessage(), t);
      throw t;
    } finally {
      unlockUniverseForUpdate();
    }

    LOG.info("Finished {} task.", getName());
  }
}
