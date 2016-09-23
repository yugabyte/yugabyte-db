// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import java.util.Collection;
import java.util.HashSet;
import java.util.Set;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.TaskList;
import com.yugabyte.yw.commissioner.tasks.params.UniverseDefinitionTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleClusterServerCtl;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleSetupServer;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleUpdateNodeInfo;
import com.yugabyte.yw.commissioner.tasks.subtasks.UniverseUpdateSucceeded;
import com.yugabyte.yw.commissioner.tasks.subtasks.UpdatePlacementInfo;
import com.yugabyte.yw.commissioner.tasks.subtasks.WaitForMasterLeader;
import com.yugabyte.yw.commissioner.tasks.subtasks.WaitForServer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.UniverseDetails;

/**
 * Abstract base class for all tasks that create/edit the universe definition. These include the
 * create universe task and all forms of edit universe tasks. Note that the delete universe task
 * extends the UniverseTaskBase, as it does not depend on the universe definition.
 */
public abstract class UniverseDefinitionTaskBase extends UniverseTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(UniverseDefinitionTaskBase.class);

  // This is the maximum number of subnets that the masters can be placed across, and need to be an
  // odd number for consensus to work.
  public static final int maxMasterSubnets = 3;

  // Enum for specifying the server type.
  public enum ServerType {
    MASTER,
    TSERVER
  }

  // The task params.
  @Override
  protected UniverseDefinitionTaskParams taskParams() {
    return (UniverseDefinitionTaskParams) taskParams;
  }

  /**
   * Writes the user intent to the universe.
   */
  public Universe writeUserIntentToUniverse() {
    // Create the update lambda.
    UniverseUpdater updater = new UniverseUpdater() {
      @Override
      public void run(Universe universe) {
        UniverseDetails universeDetails = universe.getUniverseDetails();
        // Persist the updated information about the universe.
        // It should have been marked as being edited in lockUniverseForUpdate().
        if (!universeDetails.updateInProgress) {
          String msg = "Universe " + taskParams().universeUUID +
                       " has not been marked as being updated.";
          LOG.error(msg);
          throw new RuntimeException(msg);
        }
        universeDetails.userIntent = taskParams().userIntent;
        universeDetails.placementInfo = taskParams().placementInfo;
        universeDetails.nodePrefix = taskParams().nodePrefix;
        universeDetails.numNodes = taskParams().numNodes;
        universeDetails.ybServerPkg = taskParams().ybServerPkg;
        universe.setUniverseDetails(universeDetails);
      }
    };
    // Perform the update. If unsuccessful, this will throw a runtime exception which we do not
    // catch as we want to fail.
    Universe universe = Universe.saveDetails(taskParams().universeUUID, updater);
    LOG.debug("Locked universe " + taskParams().universeUUID + " for updates");
    // Return the universe object that we have already updated.
    return universe;
  }

  // Fix up the name of all the nodes.
  public void fixNodeNames() {
    for (NodeDetails node : taskParams().newNodesSet) {
      String newName = taskParams().nodePrefix + "-n" + node.nodeIdx;
      LOG.info("Changing node name from " + node.nodeName + " to " + newName); 
      node.nodeName = newName;
    }
  }

  // Get the list of new masters to be provisioned.
  public void getNewMasters(Set<NodeDetails> newMasters) {
    for (NodeDetails node : taskParams().newNodesSet) {
      if (node.isMaster) {
        newMasters.add(node);
      }
    }
  }

  public void addNodesToUniverse(Collection<NodeDetails> nodes) {
    // Persist the desired node information into the DB.
    UniverseUpdater updater = new UniverseUpdater() {
      @Override
      public void run(Universe universe) {
        UniverseDetails universeDetails = universe.getUniverseDetails();
        for (NodeDetails node : nodes) {
          // Since we have already set the 'updateInProgress' flag on this universe in the DB and
          // this step is single threaded, we are guaranteed no one else will be modifying it.
          // Replace the entire node value.
          universeDetails.nodeDetailsMap.put(node.nodeName, node);
          LOG.debug("Adding node " + node.nodeName +
                    " to universe " + taskParams().universeUUID);
        }
      }
    };
    Universe.saveDetails(taskParams().universeUUID, updater);
    LOG.debug("Added " + nodes.size() + " nodes to universe " + taskParams().universeUUID);
  }

  /**
   * Creates a task list for provisioning the list of nodes passed in and adds it to the task queue.
   *
   * @param nodes : a collection of nodes that need to be created
   */
  public TaskList createSetupServerTasks(Collection<NodeDetails> nodes) {
    TaskList taskList = new TaskList("AnsibleSetupServer", executor);
    for (NodeDetails node : nodes) {
      AnsibleSetupServer.Params params = new AnsibleSetupServer.Params();
      // Set the cloud name.
      params.cloud = CloudType.aws;
      // Set the region code.
      params.azUuid = node.azUuid;
      // Add the node name.
      params.nodeName = node.nodeName;
      // Add the universe uuid.
      params.universeUUID = taskParams().universeUUID;
      // Pick one of the subnets in a round robin fashion.
      params.subnetId = node.cloudInfo.subnet_id;
      // Set the instance type.
      params.instanceType = taskParams().userIntent.instanceType;
      // Create the Ansible task to setup the server.
      AnsibleSetupServer ansibleSetupServer = new AnsibleSetupServer();
      ansibleSetupServer.initialize(params);
      // Add it to the task list.
      taskList.addTask(ansibleSetupServer);
    }
    taskListQueue.add(taskList);
    return taskList;
  }

  /**
   * Creates a task list for fetching information about the nodes provisioned (such as the ip
   * address) and adds it to the task queue. This is specific to the cloud.
   *
   * @param nodes : a collection of nodes that need to be created
   */
  public TaskList createServerInfoTasks(Collection<NodeDetails> nodes) {
    TaskList taskList = new TaskList("AnsibleUpdateNodeInfo", executor);
    for (NodeDetails node : nodes) {
      AnsibleUpdateNodeInfo.Params params = new AnsibleUpdateNodeInfo.Params();
      // Set the cloud name.
      params.cloud = CloudType.aws;
      // Set the region name to the proper provider code so we can use it in the cloud API calls.
      params.azUuid = node.azUuid;
      // Add the node name.
      params.nodeName = node.nodeName;
      // Add the universe uuid.
      params.universeUUID = taskParams().universeUUID;
      // Create the Ansible task to get the server info.
      AnsibleUpdateNodeInfo ansibleFindCloudHost = new AnsibleUpdateNodeInfo();
      ansibleFindCloudHost.initialize(params);
      // Add it to the task list.
      taskList.addTask(ansibleFindCloudHost);
    }
    taskListQueue.add(taskList);
    return taskList;
  }

  /**
   * Creates a task list to configure the newly provisioned nodes and adds it to the task queue.
   * Includes tasks such as setting up the 'yugabyte' user and installing the passed in software
   * package.
   *
   * @param nodes : a collection of nodes that need to be created
   * @param isMasterInShellMode : true if we are configuring a master node in shell mode
   */
  public TaskList createConfigureServerTasks(Collection<NodeDetails> nodes,
                                             boolean isMasterInShellMode) {
    TaskList taskList = new TaskList("AnsibleConfigureServers", executor);
    for (NodeDetails node : nodes) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      // Set the cloud name.
      params.cloud = CloudType.aws;
      // Add the node name.
      params.nodeName = node.nodeName;
      // Add the universe uuid.
      params.universeUUID = taskParams().universeUUID;
      // Add the az uuid.
      params.azUuid = node.azUuid;
      // Set if this node is a master in shell mode.
      params.isMasterInShellMode = isMasterInShellMode;
      // The software package to install for this cluster.
      params.ybServerPkg = taskParams().ybServerPkg;
      // Create the Ansible task to get the server info.
      AnsibleConfigureServers task = new AnsibleConfigureServers();
      task.initialize(params);
      // Add it to the task list.
      taskList.addTask(task);
    }
    taskListQueue.add(taskList);
    return taskList;
  }

  /**
   * Creates a task list to start the masters of the cluster to be created and adds it to the task
   * queue.
   *
   * @param nodes   : a collection of nodes that need to be created
   * @param isShell : Determines if the masters should be started in shell mode
   */
  public TaskList createStartMasterTasks(Collection<NodeDetails> nodes,
                                         boolean isShell) {
    TaskList taskList = new TaskList("AnsibleClusterServerCtl", executor);
    for (NodeDetails node : nodes) {
      AnsibleClusterServerCtl.Params params = new AnsibleClusterServerCtl.Params();
      // Set the cloud name.
      params.cloud = CloudType.aws;
      // Add the node name.
      params.nodeName = node.nodeName;
      // Add the universe uuid.
      params.universeUUID = taskParams().universeUUID;
      // Add the az uuid.
      params.azUuid = node.azUuid;
      // The service and the command we want to run.
      params.process = "master";
      params.command = isShell ? "start" : "create";
      // Create the Ansible task to get the server info.
      AnsibleClusterServerCtl task = new AnsibleClusterServerCtl();
      task.initialize(params);
      // Add it to the task list.
      taskList.addTask(task);
    }
    taskListQueue.add(taskList);
    return taskList;
  }

  /**
   * Creates a task list to start the tservers on the set of passed in nodes and adds it to the task
   * queue.
   *
   * @param nodes : a collection of nodes that need to be created
   */
  public TaskList createStartTServersTasks(Collection<NodeDetails> nodes) {
    TaskList taskList = new TaskList("AnsibleClusterServerCtl", executor);
    for (NodeDetails node : nodes) {
      AnsibleClusterServerCtl.Params params = new AnsibleClusterServerCtl.Params();
      // Set the cloud name.
      params.cloud = CloudType.aws;
      // Add the node name.
      params.nodeName = node.nodeName;
      // Add the universe uuid.
      params.universeUUID = taskParams().universeUUID;
      // Add the az uuid.
      params.azUuid = node.azUuid;
      // The service and the command we want to run.
      params.process = "tserver";
      params.command = "start";
      // Create the Ansible task to get the server info.
      AnsibleClusterServerCtl task = new AnsibleClusterServerCtl();
      task.initialize(params);
      // Add it to the task list.
      taskList.addTask(task);
    }
    taskListQueue.add(taskList);
    return taskList;
  }

  /**
   * Create a task list to ping all servers until they are up.
   *
   * @param nodes : a collection of nodes that need to be pinged.
   * @param type  : Master or tserver type server running on these nodes.
   */
  public TaskList createWaitForServersTasks(Collection<NodeDetails> nodes, ServerType type) {
    TaskList taskList = new TaskList("WaitForServer", executor);
    for (NodeDetails node : nodes) {
      WaitForServer.Params params = new WaitForServer.Params();
      params.universeUUID = taskParams().universeUUID;
      params.nodeName = node.nodeName;
      params.serverType = type;
      WaitForServer task = new WaitForServer();
      task.initialize(params);
      taskList.addTask(task);
    }
    taskListQueue.add(taskList);
    return taskList;
  }

  public TaskList createWaitForMasterLeaderTask() {
    TaskList taskList = new TaskList("WaitForMasterLeader", executor);
    WaitForMasterLeader task = new WaitForMasterLeader();
    WaitForMasterLeader.Params params = new WaitForMasterLeader.Params();
    params.universeUUID = taskParams().universeUUID;
    task.initialize(params);
    taskList.addTask(task);
    taskListQueue.add(taskList);
    return taskList;
  }

  /**
   * Creates a task list to update the placement information by making a call to the master leader
   * of the cluster just created and adds it to the task queue.
   */
  public TaskList createPlacementInfoTask(Set<NodeDetails> newMasters,
                                          Collection<NodeDetails> blacklistNodes) {
    TaskList taskList = new TaskList("UpdatePlacementInfo", executor);
    UpdatePlacementInfo.Params params = new UpdatePlacementInfo.Params();
    // Set the cloud name.
    params.cloud = CloudType.aws;
    // Add the universe uuid.
    params.universeUUID = taskParams().universeUUID;
    // Set the number of masters.
    params.numMasters = newMasters.size();
    // Set the blacklist nodes if any are passed in.
    if (blacklistNodes != null && !blacklistNodes.isEmpty()) {
      Set<String> blacklistNodeNames = new HashSet<String>();
      for (NodeDetails node : blacklistNodes) {
        blacklistNodeNames.add(node.nodeName);
      }
      params.blacklistNodes = blacklistNodeNames;
    }
    // Create the task to update placement info.
    UpdatePlacementInfo task = new UpdatePlacementInfo();
    task.initialize(params);
    // Add it to the task list.
    taskList.addTask(task);
    taskListQueue.add(taskList);
    return taskList;
  }

  /**
   * Create a task to mark the change on a universe as success.
   */
  public TaskList createMarkUniverseUpdateSuccessTasks() {
    TaskList taskList = new TaskList("FinalizeUniverseUpdate", executor);
    UniverseUpdateSucceeded.Params params = new UniverseUpdateSucceeded.Params();
    params.universeUUID = taskParams().universeUUID;
    UniverseUpdateSucceeded task = new UniverseUpdateSucceeded();
    task.initialize(params);
    taskList.addTask(task);
    taskListQueue.add(taskList);
    return taskList;
  }

  /**
   * Verify that the task params are valid.
   */
  public void verifyParams() {
    if (taskParams().universeUUID == null) {
      throw new RuntimeException(getName() + ": universeUUID not set");
    }
    if (taskParams().nodePrefix == null) {
      throw new RuntimeException(getName() + ": nodePrefix not set");
    }
    if (taskParams().numNodes < 3) {
      throw new RuntimeException(getName() + ": numNodes is invalid, need at least 3 nodes");
    }
    if (taskParams().userIntent.replicationFactor < 3) {
      throw new RuntimeException(getName() + ": replicationFactor is invalid, needs to be at least 3.");
    }
  }
}
