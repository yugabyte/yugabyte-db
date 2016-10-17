// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import java.util.ArrayList;
import java.util.Collection;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.TaskList;
import com.yugabyte.yw.commissioner.TaskListQueue;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskType;
import com.yugabyte.yw.commissioner.tasks.subtasks.ChangeMasterConfig;
import com.yugabyte.yw.commissioner.tasks.subtasks.WaitForDataMove;
import com.yugabyte.yw.commissioner.tasks.subtasks.WaitForLoadBalance;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.models.helpers.NodeDetails;

// Tracks edit intents to the cluster and then performs the sequence of configuration changes on
// this universe to go from the current set of master/tserver nodes to the final configuration.
public class EditUniverse extends UniverseDefinitionTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(EditUniverse.class);

  // Get the new masters from the node list.
  Set<NodeDetails> newMasters = new HashSet<NodeDetails>();

  @Override
  public void run() {
    LOG.info("Started {} task for uuid={}", getName(), taskParams().universeUUID);

    try {
      // Verify the task params.
      verifyParams();

      // Create the task list sequence.
      taskListQueue = new TaskListQueue();

      // Update the universe DB with the changes to be performed and set the 'updateInProgress' flag
      // to prevent other updates from happening.
      lockUniverseForUpdate(taskParams().expectedUniverseVersion);

      // Update the user intent.
      writeUserIntentToUniverse();

      // Set the correct node names as they are finalized now. This is done just in case the user
      // changes the universe name before submitting. 
      updateNodeNames();

      LOG.info("Configure numNodes={}, numMasters={}",
               taskParams().userIntent.numNodes, taskParams().userIntent.replicationFactor);

      Collection<NodeDetails> blacklistNodes =
          PlacementInfoUtil.getTserversToBeRemoved(taskParams().nodeDetailsSet);

      Collection<NodeDetails> nodesToProvision =
              PlacementInfoUtil.getNodesToProvision(taskParams().nodeDetailsSet);

      // Set the old nodes' state to to-be-decommissioned.
      if (!blacklistNodes.isEmpty()) {
        createSetNodeStateTasks(blacklistNodes, NodeDetails.NodeState.ToBeDecommissioned)
            .setUserSubTask(SubTaskType.Provisioning);
      }

      // Create the required number of nodes in the appropriate locations.
      createSetupServerTasks(nodesToProvision).setUserSubTask(SubTaskType.Provisioning);

      // Get all information about the nodes of the cluster. This includes the public ip address,
      // the private ip address (in the case of AWS), etc.
      createServerInfoTasks(nodesToProvision).setUserSubTask(SubTaskType.Provisioning);

      // Configures and deploys software on all the nodes (masters and tservers).
      createConfigureServerTasks(nodesToProvision, true /* isShell */)
          .setUserSubTask(SubTaskType.InstallingSoftware);

      newMasters = PlacementInfoUtil.getMastersToProvision(taskParams().nodeDetailsSet);

      // Creates the YB cluster by starting the masters in the shell mode.
      if (!newMasters.isEmpty()) {
        createStartMasterTasks(
            newMasters, true /* isShell */).setUserSubTask(SubTaskType.ConfigureUniverse);

        // Wait for masters to be responsive.
        createWaitForServersTasks(
            newMasters, ServerType.MASTER).setUserSubTask(SubTaskType.ConfigureUniverse);
      }

      Set<NodeDetails> newTservers =
          PlacementInfoUtil.getTserversToProvision(taskParams().nodeDetailsSet);

      if (!newTservers.isEmpty()) {
        // Start the tservers in the clusters.
        createStartTServersTasks(newTservers).setUserSubTask(SubTaskType.ConfigureUniverse);

        // Wait for all tablet servers to be responsive.
        createWaitForServersTasks(
            newTservers, ServerType.TSERVER).setUserSubTask(SubTaskType.ConfigureUniverse);
      }

      // Set the new nodes' state to running.
      createSetNodeStateTasks(nodesToProvision, NodeDetails.NodeState.Running)
          .setUserSubTask(SubTaskType.ConfigureUniverse);

      if (!newMasters.isEmpty()) {
        // Now finalize the cluster configuration change tasks.
        createMoveMastersTasks(SubTaskType.WaitForDataMigration);
      }

      if (!blacklistNodes.isEmpty()) {
        // Persist the placement info and blacklisted node info into the YB master.
        // This is done after master config change jobs, so that the new master leader can perform
        // the auto load-balancing, and all tablet servers are heart beating to new set of masters.
        createPlacementInfoTask(blacklistNodes).setUserSubTask(SubTaskType.WaitForDataMigration);

        // Wait for %age completion of the tablet move from master.
        createWaitForDataMoveTask().setUserSubTask(SubTaskType.WaitForDataMigration);

        // Send destroy old set of nodes to ansible and remove them from this universe.
        createDestroyServerTasks(blacklistNodes).setUserSubTask(SubTaskType.RemovingUnusedServers);

        // Clearing the blacklist on the yb cluster master is handled on the server side.
      } else {
        // If only tservers are added or removed, wait for load to balance across all tservers.
        createWaitForLoadBalanceTask().setUserSubTask(SubTaskType.WaitForDataMigration);
      }

      // Marks the update of this universe as a success only if all the tasks before it succeeded.
      createMarkUniverseUpdateSuccessTasks();

      // Run all the tasks.
      taskListQueue.run();
    } catch (Throwable t) {
      LOG.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);
      throw t;
    } finally {
      // Mark the update of the universe as done. This will allow future edits/updates to the
      // universe to happen.
      unlockUniverseForUpdate();
    }
    LOG.info("Finished {} task.", getName());
  }

  /**
   * Fills in the series of steps needed to move the masters using the tag names of the nodes. The
   * actual node details (such as their ip addresses) are found at runtime by querying the database.
   */
  private void createMoveMastersTasks(UserTaskDetails.SubTaskType subTask) {
    // Get the list of node names to add as masters.
    List<String> mastersToAdd = new ArrayList<String>();
    for (NodeDetails node : newMasters) {
      mastersToAdd.add(node.nodeName);
    }

    Collection<NodeDetails> removeMasters =
        PlacementInfoUtil.getMastersToBeRemoved(taskParams().nodeDetailsSet);

    // Get the list of node names to remove as masters.
    List<String> mastersToRemove = new ArrayList<String>();
    for (NodeDetails node : removeMasters) {
      mastersToRemove.add(node.nodeName);
    }

    // Find the minimum number of master changes where we can perform an add followed by a remove.
    int numIters = Math.min(mastersToAdd.size(), mastersToRemove.size());

    // Perform a master add followed by a remove if possible. Need not remove the (current) master
    // leader last - even if we get current leader, it might change by the time we run the actual
    // task. So we might do multiple leader stepdown's, which happens automatically on in the
    // client code during the task's run.
    for (int idx = 0; idx < numIters; idx++) {
      createChangeConfigTask(mastersToAdd.get(idx), true, subTask);
      createChangeConfigTask(mastersToRemove.get(idx), false, subTask);
    }

    // Perform any additions still left.
    for (int idx = numIters; idx < newMasters.size(); idx++) {
      createChangeConfigTask(mastersToAdd.get(idx), true, subTask);
    }

    // Perform any removals still left.
    for (int idx = numIters; idx < removeMasters.size(); idx++) {
      createChangeConfigTask(mastersToRemove.get(idx), false, subTask);
    }
  }

  private void createChangeConfigTask(String nodeName,
                                      boolean isAdd,
                                      UserTaskDetails.SubTaskType subTask) {
    // Create a new task list for the change config so that it happens one by one.
    String taskListName = "ChangeMasterConfig(" + nodeName + ", " + (isAdd?"add":"remove") + ")";
    TaskList taskList = new TaskList(taskListName, executor);
    // Create the task params.
    ChangeMasterConfig.Params params = new ChangeMasterConfig.Params();
    // Set the cloud name.
    params.cloud = CloudType.aws;
    // Add the node name.
    params.nodeName = nodeName;
    // Add the universe uuid.
    params.universeUUID = taskParams().universeUUID;
    // This is an add master.
    params.opType = isAdd ? ChangeMasterConfig.OpType.AddMaster :
                            ChangeMasterConfig.OpType.RemoveMaster;
    // Create the task.
    ChangeMasterConfig changeConfig = new ChangeMasterConfig();
    changeConfig.initialize(params);
    // Add it to the task list.
    taskList.addTask(changeConfig);
    // Add the task list to the task queue.
    taskListQueue.add(taskList);
    // Configure the user facing subtask for this task list.
    taskList.setUserSubTask(subTask);
  }

  private TaskList createWaitForDataMoveTask() {
    TaskList taskList = new TaskList("WaitForDataMove", executor);
    WaitForDataMove.Params params = new WaitForDataMove.Params();
    params.universeUUID = taskParams().universeUUID;
    // Create the task.
    WaitForDataMove waitForMove = new WaitForDataMove();
    waitForMove.initialize(params);
    // Add it to the task list.
    taskList.addTask(waitForMove);
    // Add the task list to the task queue.
    taskListQueue.add(taskList);
    return taskList;
  }
  
  private TaskList createWaitForLoadBalanceTask() {
    TaskList taskList = new TaskList("WaitForDataMove", executor);
    WaitForLoadBalance.Params params = new WaitForLoadBalance.Params();
    params.universeUUID = taskParams().universeUUID;
    // Create the task.
    WaitForLoadBalance waitForLoadBalance = new WaitForLoadBalance();
    waitForLoadBalance.initialize(params);
    // Add it to the task list.
    taskList.addTask(waitForLoadBalance);
    // Add the task list to the task queue.
    taskListQueue.add(taskList);
    return taskList;
  }
}
