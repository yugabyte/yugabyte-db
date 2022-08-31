package com.yugabyte.yw.commissioner.tasks;

import java.util.Arrays;
import java.util.Collections;
import java.util.HashSet;

import javax.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.NodeActionType;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;

import lombok.extern.slf4j.Slf4j;

@Slf4j
public class RebootNodeInUniverse extends UniverseTaskBase {

  @Inject
  protected RebootNodeInUniverse(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected NodeTaskParams taskParams() {
    return (NodeTaskParams) taskParams;
  }

  @Override
  public void run() {
    NodeDetails currentNode = null;

    try {
      checkUniverseVersion();

      // Set the 'updateInProgress' flag to prevent other updates from happening.
      Universe universe = lockUniverseForUpdate(taskParams().expectedUniverseVersion);
      currentNode = universe.getNode(taskParams().nodeName);

      if (currentNode == null) {
        String msg = "No node " + taskParams().nodeName + " found in universe " + universe.name;
        log.error(msg);
        throw new RuntimeException(msg);
      }

      taskParams().azUuid = currentNode.azUuid;
      taskParams().placementUuid = currentNode.placementUuid;

      if (!instanceExists(taskParams())) {
        String msg = "No instance exists for " + taskParams().nodeName;
        log.error(msg);
        throw new RuntimeException(msg);
      }

      currentNode.validateActionOnState(NodeActionType.REBOOT);

      preTaskActions();

      createSetNodeStateTask(currentNode, NodeState.Rebooting)
          .setSubTaskGroupType(SubTaskGroupType.RebootingNode);

      boolean hasMaster = currentNode.isMaster;

      // Stop the tserver.
      createTServerTaskForNode(currentNode, "stop")
          .setSubTaskGroupType(SubTaskGroupType.RebootingNode);

      // Stop the master process on this node.
      if (hasMaster) {
        createStopMasterTasks(new HashSet<NodeDetails>(Arrays.asList(currentNode)))
            .setSubTaskGroupType(SubTaskGroupType.RebootingNode);
        createWaitForMasterLeaderTask().setSubTaskGroupType(SubTaskGroupType.RebootingNode);
      }

      // Reboot the node.
      createRebootTasks(Collections.singletonList(currentNode))
          .setSubTaskGroupType(SubTaskGroupType.RebootingNode);

      if (hasMaster) {
        // Start the master.
        createStartMasterTasks(new HashSet<NodeDetails>(Arrays.asList(currentNode)))
            .setSubTaskGroupType(SubTaskGroupType.RebootingNode);

        // Wait for the master to be responsive.
        createWaitForServersTasks(
                new HashSet<NodeDetails>(Arrays.asList(currentNode)), ServerType.MASTER)
            .setSubTaskGroupType(SubTaskGroupType.RebootingNode);

        createWaitForServerReady(
                currentNode, ServerType.MASTER, getSleepTimeForProcess(ServerType.MASTER))
            .setSubTaskGroupType(SubTaskGroupType.RebootingNode);
      }

      // Start the tserver.
      createTServerTaskForNode(currentNode, "start")
          .setSubTaskGroupType(SubTaskGroupType.RebootingNode);

      // Wait for the tablet server to be responsive.
      createWaitForServersTasks(
              new HashSet<NodeDetails>(Arrays.asList(currentNode)), ServerType.TSERVER)
          .setSubTaskGroupType(SubTaskGroupType.RebootingNode);

      createWaitForServerReady(
              currentNode, ServerType.TSERVER, getSleepTimeForProcess(ServerType.TSERVER))
          .setSubTaskGroupType(SubTaskGroupType.RebootingNode);

      // Update node state to running.
      createSetNodeStateTask(currentNode, NodeDetails.NodeState.Live)
          .setSubTaskGroupType(SubTaskGroupType.RebootingNode);

      // Marks the update of this universe as a success only if all the tasks before it succeeded.
      createMarkUniverseUpdateSuccessTasks().setSubTaskGroupType(SubTaskGroupType.RebootingNode);

      getRunnableTask().runSubTasks();
    } catch (Throwable t) {
      log.error("Error executing task {}, error='{}'", getName(), t.getMessage(), t);
      // Reset the state, on any failure, so that the actions can be retried.
      if (currentNode != null) {
        setNodeState(taskParams().nodeName, currentNode.state);
      }

      throw t;
    } finally {
      unlockUniverseForUpdate();
    }
  }
}
