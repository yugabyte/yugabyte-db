// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import java.util.ArrayList;
import java.util.Collection;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.TaskList;
import com.yugabyte.yw.commissioner.TaskListQueue;
import com.yugabyte.yw.commissioner.tasks.subtasks.ChangeMasterConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.NodeDetails;

// Tracks edit intents to the cluster and then performs the sequence of configuration changes on
// this universe to go from the current set of master/tserver nodes to the final configuration.
public class EditUniverse extends UniverseTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(EditUniverse.class);

  // The set of new nodes that need to be created.
  Map<String, NodeDetails> newNodesMap = new HashMap<String, NodeDetails>();

  // The subset of new nodes that are masters.
  Set<NodeDetails> newMasters = new HashSet<NodeDetails>();

  // Initial state of nodes in this universe before editing it.
  Collection<NodeDetails> existingNodes;

  // Initial set of masters in this universe before editing it.
  Collection<NodeDetails> existingMasters;

  @Override
  public void run() {
    LOG.info("Started {} task for uuid={}", getName(), taskParams.universeUUID);

    try {
      // Create the task list sequence.
      taskListQueue = new TaskListQueue();

      // Update the universe DB with the update to be performed and set the 'updateInProgress' flag
      // to prevent other updates from happening.
      Universe universe = lockUniverseForUpdate();

      // Get the existing nodes.
      existingNodes = universe.getNodes();
      // Get the existing masters.
      existingMasters = universe.getMasters();
      // Get the number of masters currently present in the cluster.
      int numMasters = existingMasters.size();
      // For now, we provision the same number of nodes as before.
      taskParams.numNodes = existingNodes.size();
      // Get the index of the largest node. We start from an index greater than that.
      int maxNodeIdx = -1;
      for (NodeDetails node : existingNodes) {
        if (node.nodeIdx > maxNodeIdx) {
          maxNodeIdx = node.nodeIdx;
        }
      }
      int startNodeIndex = maxNodeIdx + 1;

      // Configure the new cluster nodes.
      configureNewNodes(universe.universeDetails.nodePrefix,
                        startNodeIndex,
                        numMasters,
                        newNodesMap,
                        newMasters);

      // Add the newly configured nodes into the universe.
      addNodesToUniverse(newNodesMap.values());

      // Create the required number of nodes in the appropriate locations.
      createSetupServerTasks(newNodesMap.values());

      // Get all information about the nodes of the cluster. This includes the public ip address,
      // the private ip address (in the case of AWS), etc.
      createServerInfoTasks(newNodesMap.values());

      // Configures and deploys software on all the nodes (masters and tservers).
      createConfigureServerTasks(newNodesMap.values());

      // Creates the YB cluster by starting the masters in the shell mode.
      createClusterStartTasks(newMasters, true /* isShell */);

      // Start the tservers in the clusters.
      createStartTServersTasks(newNodesMap.values());

      // Now finalize the cluster configuration change tasks. This adds directly to the global
      // list as only one change config job can be done in one step.
      createMoveMastersTasks();

      // Persist the placement info and blacklisted node info into the YB master.
      // This is done after master config change jobs, so that the new master leader can perform
      // the auto load-balancing, and all tablet servers are heart beating to new set of masters.
      createPlacementInfoTask(existingNodes);

      // Wait for %age completion of the tablet move from master.

      // Finally send destroy old set of nodes to ansible.

      // Marks the update of this universe as a success only if all the tasks before it succeeded.
      createMarkUniverseUpdateSuccessTasks();

      // Run all the tasks.
      taskListQueue.run();
    } catch (Throwable t) {
      LOG.error("Error executing task {} with error={}.", getName(), t);
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
  private void createMoveMastersTasks() {
    // Get the list of node names to add as masters.
    List<String> mastersToAdd = new ArrayList<String>();
    for (NodeDetails node : newMasters) {
      mastersToAdd.add(node.instance_name);
    }
    // Get the list of node names to remove as masters.
    List<String> mastersToRemove = new ArrayList<String>();
    for (NodeDetails node : existingMasters) {
      mastersToRemove.add(node.instance_name);
    }
    // Find the minimum number of master changes where we can perform an add followed by a remove.
    int numIters = Math.min(mastersToAdd.size(), mastersToRemove.size());

    // Perform a master add followed by a remove if possible.
    // TODO: Find the master leader and remove it last. The step down will happen in client code.
    for (int idx = 0; idx < numIters; idx++) {
      createChangeConfigTask(mastersToAdd.get(idx), true);
      createChangeConfigTask(mastersToRemove.get(idx), false);
    }

    // Perform any additions still left.
    for (int idx = numIters; idx < newMasters.size(); idx++) {
      createChangeConfigTask(mastersToAdd.get(idx), true);
    }
    // Perform any removals still left.
    for (int idx = numIters; idx < existingMasters.size(); idx++) {
      createChangeConfigTask(mastersToRemove.get(idx), false);
    }
    LOG.info("Change Creation creation done");
  }

  private void createChangeConfigTask(String nodeName, boolean isAdd) {
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
    params.universeUUID = taskParams.universeUUID;
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
  }
}
