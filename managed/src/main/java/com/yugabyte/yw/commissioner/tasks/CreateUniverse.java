// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.commissioner.TaskListQueue;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskType;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;

public class CreateUniverse extends UniverseDefinitionTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(CreateUniverse.class);

  // The set of new nodes that need to be created.
  protected Map<String, NodeDetails> newNodesMap = new HashMap<String, NodeDetails>();

  // The subset of new nodes that are masters.
  protected Set<NodeDetails> newMasters = new HashSet<NodeDetails>();

  @Override
  public void run() {
    LOG.info("Started {} task.", getName());
    try {
      // Verify the task params.
      verifyParams();

      // Create the task list sequence.
      taskListQueue = new TaskListQueue();

      // Update the universe DB with the update to be performed and set the 'updateInProgress' flag
      // to prevent other updates from happening.
      Universe universe = lockUniverseForUpdate();

      // Update the user intent.
      universe = writeUserIntentToUniverse();

      // Configure the cluster nodes.
      configureNewNodes(universe.getUniverseDetails().nodePrefix,
                        1 /* nodeStartIndex */,
                        taskParams().userIntent.replicationFactor,
                        newNodesMap,
                        newMasters);

      // Add the newly configured nodes into the universe.
      addNodesToUniverse(newNodesMap.values());

      // Create the required number of nodes in the appropriate locations.
      createSetupServerTasks(newNodesMap.values()).setUserSubTask(SubTaskType.Provisioning);

      // Get all information about the nodes of the cluster. This includes the public ip address,
      // the private ip address (in the case of AWS), etc.
      createServerInfoTasks(newNodesMap.values()).setUserSubTask(SubTaskType.Provisioning);

      // Configures and deploys software on all the nodes (masters and tservers).
      createConfigureServerTasks(
          newNodesMap.values(), false /* isShell */).setUserSubTask(SubTaskType.InstallingSoftware);

      // Creates the YB cluster by starting the masters in the create mode.
      createStartMasterTasks(
          newMasters, false /* isShell */).setUserSubTask(SubTaskType.ConfigureUniverse);

      // Wait for new masters to be responsive.
      createWaitForServersTasks(
           newMasters, ServerType.MASTER).setUserSubTask(SubTaskType.ConfigureUniverse);

      // Start the tservers in the clusters.
      createStartTServersTasks(newNodesMap.values()).setUserSubTask(SubTaskType.ConfigureUniverse);

      // Wait for new tablet servers to be responsive.
      createWaitForServersTasks(
           newNodesMap.values(), ServerType.TSERVER).setUserSubTask(SubTaskType.ConfigureUniverse);

      // Wait for a Master Leader to be elected.
      createWaitForMasterLeaderTask().setUserSubTask(SubTaskType.ConfigureUniverse);

      // Persist the placement info into the YB master.
      createPlacementInfoTask(
          newMasters, null /* blacklistNodes */).setUserSubTask(SubTaskType.ConfigureUniverse);

      // Marks the update of this universe as a success only if all the tasks before it succeeded.
      createMarkUniverseUpdateSuccessTasks();

      // Run all the tasks.
      taskListQueue.run();
    } catch (Throwable t) {
      LOG.error("Error executing task {}, error={}", getName(), t);
      throw t;
    } finally {
      // Mark the update of the universe as done. This will allow future edits/updates to the
      // universe to happen.
      unlockUniverseForUpdate();
    }
    LOG.info("Finished {} task.", getName());
  }
}
