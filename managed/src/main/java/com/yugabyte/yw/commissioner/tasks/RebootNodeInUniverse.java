package com.yugabyte.yw.commissioner.tasks;

import java.util.Collections;

import javax.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.NodeActionType;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;

import lombok.extern.slf4j.Slf4j;

import static com.yugabyte.yw.models.helpers.NodeDetails.NodeState.apiAdditionalAllowedActions;

@Slf4j
public class RebootNodeInUniverse extends UniverseDefinitionTaskBase {

  public static class Params extends NodeTaskParams {
    public boolean isHardReboot = false;
  }

  @Inject
  protected RebootNodeInUniverse(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
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

      boolean isHardReboot = taskParams().isHardReboot;
      NodeActionType nodeAction = isHardReboot ? NodeActionType.HARD_REBOOT : NodeActionType.REBOOT;
      NodeState nodeState = isHardReboot ? NodeState.HardRebooting : NodeState.Rebooting;
      SubTaskGroupType subTaskGroup =
          isHardReboot ? SubTaskGroupType.HardRebootingNode : SubTaskGroupType.RebootingNode;

      currentNode.validateActionOnState(nodeAction);

      preTaskActions();

      createSetNodeStateTask(currentNode, nodeState)
          .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);

      boolean hasMaster = currentNode.isMaster;

      // Stop the tserver.
      createTServerTaskForNode(currentNode, "stop")
          .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);

      // Stop Yb-controller on this node.
      if (universe.isYbcEnabled()) {
        createStopYbControllerTasks(Collections.singletonList(currentNode))
            .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
      }

      // Stop the master process on this node.
      if (hasMaster) {
        createStopMasterTasks(Collections.singleton(currentNode))
            .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
        createWaitForMasterLeaderTask().setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
      }

      // Reboot the node.
      createRebootTasks(Collections.singletonList(currentNode), isHardReboot)
          .setSubTaskGroupType(subTaskGroup);

      if (hasMaster) {
        // Start the master.
        createStartMasterTasks(Collections.singleton(currentNode))
            .setSubTaskGroupType(SubTaskGroupType.StartingMasterProcess);

        // Wait for the master to be responsive.
        createWaitForServersTasks(Collections.singleton(currentNode), ServerType.MASTER)
            .setSubTaskGroupType(SubTaskGroupType.StartingMasterProcess);

        createWaitForServerReady(
                currentNode, ServerType.MASTER, getSleepTimeForProcess(ServerType.MASTER))
            .setSubTaskGroupType(SubTaskGroupType.StartingMasterProcess);
      }

      // Start the tserver.
      createTServerTaskForNode(currentNode, "start")
          .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);

      // Wait for the tablet server to be responsive.
      createWaitForServersTasks(Collections.singleton(currentNode), ServerType.TSERVER)
          .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);

      createWaitForServerReady(
              currentNode, ServerType.TSERVER, getSleepTimeForProcess(ServerType.TSERVER))
          .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);

      if (universe.isYbcEnabled()) {
        createStartYbcProcessTasks(
            Collections.singleton(currentNode),
            universe.getUniverseDetails().getPrimaryCluster().userIntent.useSystemd);
      }

      // Update node state to running.
      createSetNodeStateTask(currentNode, NodeDetails.NodeState.Live)
          .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);

      // Marks the update of this universe as a success only if all the tasks before it succeeded.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);

      getRunnableTask().runSubTasks();
    } catch (Throwable t) {
      log.error("Error executing task {}, error='{}'", getName(), t.getMessage(), t);
      throw t;
    } finally {
      unlockUniverseForUpdate();
    }
  }
}
