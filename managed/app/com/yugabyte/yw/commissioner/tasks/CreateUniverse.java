// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.commissioner.TaskListQueue;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.NodeDetails;

public class CreateUniverse extends UniverseTaskBase {
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

      // Configure the cluster nodes.
      configureNewNodes(universe.universeDetails.nodePrefix,
                        1 /* nodeStartIndex */,
                        defaultNumMastersToChoose,
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

      // Creates the YB cluster by starting the masters in the create mode.
      createClusterStartTasks(newMasters, false /* isShell */);

      // Persist the placement info into the YB master.
      createPlacementInfoTask(null /* blacklistNodes */);

      // Start the tservers in the clusters.
      createStartTServersTasks(newNodesMap.values());

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

  private void verifyParams() {
    if (taskParams.universeUUID == null) {
      throw new RuntimeException(getName() + ": universeUUID not set");
    }
    if (taskParams.nodePrefix == null) {
      throw new RuntimeException(getName() + ": nodePrefix not set");
    }
    if (taskParams.numNodes < 3) {
      throw new RuntimeException(getName() + ": numNodes is invalid, need at least 3 nodes");
    }
  }
}
