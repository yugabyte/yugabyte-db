// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType;

import java.util.Arrays;
import java.util.HashSet;
import static com.yugabyte.yw.common.Util.areMastersUnderReplicated;

/**
 * Class contains the tasks to start a node in a given universe.
 * It starts the tserver process and the master process if needed.
 */
public class StartNodeInUniverse extends UniverseTaskBase {

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
      LOG.info("Start Node with name {} from universe {}", taskParams().nodeName, taskParams().universeUUID);

      NodeDetails currentNode = universe.getNode(taskParams().nodeName);
      if (currentNode == null) {
        String msg = "No node " + taskParams().nodeName + " found in universe " + universe.name;
        LOG.error(msg);
        throw new RuntimeException(msg);
      }

      // Update node state to Starting
      createSetNodeStateTask(currentNode, NodeState.Starting)
          .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);
      UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();

      // Start the tserver process
      createTServerTaskForNode(currentNode, "start")
          .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);

      // Mark the node process flags as true.
      createUpdateNodeProcessTask(taskParams().nodeName, ServerType.TSERVER, true)
          .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);

      if (areMastersUnderReplicated(currentNode, universe)) {
        // Start a master process.
        createStartMasterTasks(new HashSet<NodeDetails>(Arrays.asList(currentNode)))
            .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);

        // Mark node as isMaster in YW DB.
        createUpdateNodeProcessTask(taskParams().nodeName, ServerType.MASTER, true)
            .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);
      }

      // Update node state to running
      createSetNodeStateTask(currentNode, NodeDetails.NodeState.Live)
          .setSubTaskGroupType(SubTaskGroupType.StartingNode);

      // Mark universe update success to true
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.StartingNode);

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
