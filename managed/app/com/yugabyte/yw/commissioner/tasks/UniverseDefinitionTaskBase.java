// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import java.util.ArrayList;
import java.util.Collection;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Set;
import java.util.TreeSet;

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
import com.yugabyte.yw.models.helpers.PlacementInfo.PlacementAZ;
import com.yugabyte.yw.models.helpers.PlacementInfo.PlacementCloud;
import com.yugabyte.yw.models.helpers.PlacementInfo.PlacementRegion;
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

  // The task params.
  @Override
  protected UniverseDefinitionTaskParams taskParams() {
    return (UniverseDefinitionTaskParams) taskParams;
  }

  /**
   * Configures the set of nodes to be created and saves it to the universe info table.
   *
   * @param nodePrefix     : the node name prefix to use
   * @param nodeStartIndex : id from which the new nodes should be numbered
   * @param numMasters     : the number of masters desired in the edited universe
   * @param newNodesMap    : out parameter, set of new nodes being created in the new universe
   * @param newMasters     : out parameter, the subset of 'nodes' which are masters
   */
  public void configureNewNodes(String nodePrefix,
                                int nodeStartIndex,
                                int numMasters,
                                Map<String, NodeDetails> newNodesMap,
                                Set<NodeDetails> newMasters) {

    // Create the names and known properties of all the cluster nodes.
    int cloudIdx = 0;
    int regionIdx = 0;
    int azIdx = 0;
    for (int nodeIdx = nodeStartIndex; nodeIdx < taskParams().numNodes + nodeStartIndex; nodeIdx++) {
      NodeDetails nodeDetails = new NodeDetails();
      // Create the node name.
      nodeDetails.instance_name = nodePrefix + "-n" + nodeIdx;
      // Set the cloud.
      PlacementCloud placementCloud = taskParams().placementInfo.cloudList.get(cloudIdx);
      nodeDetails.cloud = placementCloud.name;
      // Set the region.
      PlacementRegion placementRegion = placementCloud.regionList.get(regionIdx);
      nodeDetails.region = placementRegion.code;
      // Set the AZ and the subnet.
      PlacementAZ placementAZ = placementRegion.azList.get(azIdx);
      nodeDetails.azUuid = placementAZ.uuid;
      nodeDetails.az = placementAZ.name;
      nodeDetails.subnet_id = placementAZ.subnet;
      // Set the tablet server role to true.
      nodeDetails.isTserver = true;
      // Set the node id.
      nodeDetails.nodeIdx = nodeIdx;
      // Add the node to the list of nodes.
      newNodesMap.put(nodeDetails.instance_name, nodeDetails);
      LOG.debug("Placed new node {} in universe {} at cloud:{}, region:{}, az:{}.",
                nodeDetails.toString(), taskParams().universeUUID, cloudIdx, regionIdx, azIdx);

      // Advance to the next az/region/cloud combo.
      azIdx = (azIdx + 1) % placementRegion.azList.size();
      regionIdx = (regionIdx + (azIdx == 0 ? 1 : 0)) % placementCloud.regionList.size();
      cloudIdx = (cloudIdx + (azIdx == 0 && regionIdx == 0 ? 1 : 0)) %
        taskParams().placementInfo.cloudList.size();
    }

    // Select the masters for this cluster based on subnets.
    List<String> masters = selectMasters(newNodesMap, numMasters);
    for (String nodeName : masters) {
      NodeDetails node = newNodesMap.get(nodeName);
      // Add the node to the new masters set.
      newMasters.add(node);
      // Mark the node as a master.
      node.isMaster = true;
    }
  }


  /**
   * Given a set of nodes and the number of masters, selects the masters.
   *
   * @param nodesMap   : a map of node names to the NodeDetails object
   * @param numMasters : the number of masters to choose
   * @return the list of node names selected as the master
   */
  public static List<String> selectMasters(Map<String, NodeDetails> nodesMap, int numMasters) {
    // Group the cluster nodes by subnets.
    Map<String, TreeSet<String>> subnetsToNodenameMap = new HashMap<String, TreeSet<String>>();
    for (Entry<String, NodeDetails> entry : nodesMap.entrySet()) {
      TreeSet<String> nodeSet = subnetsToNodenameMap.get(entry.getValue().subnet_id);
      // If the node set is empty, create it.
      if (nodeSet == null) {
        nodeSet = new TreeSet<String>();
      }
      // Add the node name into the node set.
      nodeSet.add(entry.getKey());
      // Add the node set back into the map.
      subnetsToNodenameMap.put(entry.getValue().subnet_id, nodeSet);
    }

    // Choose the masters such that we have one master per subnet if there are enough subnets.
    List<String> masters = new ArrayList<String>();
    if (subnetsToNodenameMap.size() >= maxMasterSubnets) {
      for (Entry<String, TreeSet<String>> entry : subnetsToNodenameMap.entrySet()) {
        // Get one node from each subnet.
        String nodeName = entry.getValue().first();
        masters.add(nodeName);
        LOG.info("Chose node {} as a master from subnet {}.", nodeName, entry.getKey());
        if (masters.size() == numMasters) {
          break;
        }
      }
    } else {
      // We do not have enough subnets. Simply pick enough masters.
      for (NodeDetails node : nodesMap.values()) {
        masters.add(node.instance_name);
        LOG.info("Chose node {} as a master from subnet {}.", node.instance_name, node.subnet_id);
        if (masters.size() == numMasters) {
          break;
        }
      }
    }

    // Return the list of master node names.
    return masters;
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
          universeDetails.nodeDetailsMap.put(node.instance_name, node);
          LOG.debug("Adding node " + node.instance_name +
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
  public void createSetupServerTasks(Collection<NodeDetails> nodes) {
    TaskList taskList = new TaskList("AnsibleSetupServer", executor);
    for (NodeDetails node : nodes) {
      AnsibleSetupServer.Params params = new AnsibleSetupServer.Params();
      // Set the cloud name.
      params.cloud = CloudType.aws;
      // Set the region code.
      params.azUuid = node.azUuid;
      // Add the node name.
      params.nodeName = node.instance_name;
      // Add the universe uuid.
      params.universeUUID = taskParams().universeUUID;
      // Pick one of the subnets in a round robin fashion.
      params.subnetId = node.subnet_id;
      // Create the Ansible task to setup the server.
      AnsibleSetupServer ansibleSetupServer = new AnsibleSetupServer();
      ansibleSetupServer.initialize(params);
      // Add it to the task list.
      taskList.addTask(ansibleSetupServer);
    }
    taskListQueue.add(taskList);
  }

  /**
   * Creates a task list for fetching information about the nodes provisioned (such as the ip
   * address) and adds it to the task queue. This is specific to the cloud.
   *
   * @param nodes : a collection of nodes that need to be created
   */
  public void createServerInfoTasks(Collection<NodeDetails> nodes) {
    TaskList taskList = new TaskList("AnsibleUpdateNodeInfo", executor);
    for (NodeDetails node : nodes) {
      AnsibleUpdateNodeInfo.Params params = new AnsibleUpdateNodeInfo.Params();
      // Set the cloud name.
      params.cloud = CloudType.aws;
      // Set the region name to the proper provider code so we can use it in the cloud API calls.
      params.azUuid = node.azUuid;
      // Add the node name.
      params.nodeName = node.instance_name;
      // Add the universe uuid.
      params.universeUUID = taskParams().universeUUID;
      // Create the Ansible task to get the server info.
      AnsibleUpdateNodeInfo ansibleFindCloudHost = new AnsibleUpdateNodeInfo();
      ansibleFindCloudHost.initialize(params);
      // Add it to the task list.
      taskList.addTask(ansibleFindCloudHost);
    }
    taskListQueue.add(taskList);
  }

  /**
   * Creates a task list to configure the newly provisioned nodes and adds it to the task queue.
   * Includes tasks such as setting up the 'yugabyte' user and installing the passed in software
   * package.
   *
   * @param nodes : a collection of nodes that need to be created
   */
  public void createConfigureServerTasks(Collection<NodeDetails> nodes) {
    TaskList taskList = new TaskList("AnsibleConfigureServers", executor);
    for (NodeDetails node : nodes) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      // Set the cloud name.
      params.cloud = CloudType.aws;
      // Add the node name.
      params.nodeName = node.instance_name;
      // Add the universe uuid.
      params.universeUUID = taskParams().universeUUID;
      // Add the az uuid.
      params.azUuid = node.azUuid;
      // The software package to install for this cluster.
      params.ybServerPkg = taskParams().ybServerPkg;
      // Create the Ansible task to get the server info.
      AnsibleConfigureServers task = new AnsibleConfigureServers();
      task.initialize(params);
      // Add it to the task list.
      taskList.addTask(task);
    }
    taskListQueue.add(taskList);
  }

  /**
   * Creates a task list to start the masters of the cluster to be created and adds it to the task
   * queue.
   *
   * @param nodes   : a collection of nodes that need to be created
   * @param isShell : Determines if the masters should be started in shell mode
   */
  public void createClusterStartTasks(Collection<NodeDetails> nodes,
                                      boolean isShell) {
    TaskList taskList = new TaskList("AnsibleClusterServerCtl", executor);
    for (NodeDetails node : nodes) {
      AnsibleClusterServerCtl.Params params = new AnsibleClusterServerCtl.Params();
      // Set the cloud name.
      params.cloud = CloudType.aws;
      // Add the node name.
      params.nodeName = node.instance_name;
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
  }

  /**
   * Creates a task list to start the tservers on the set of passed in nodes and adds it to the task
   * queue.
   *
   * @param nodes : a collection of nodes that need to be created
   */
  public void createStartTServersTasks(Collection<NodeDetails> nodes) {
    TaskList taskList = new TaskList("AnsibleClusterServerCtl", executor);
    for (NodeDetails node : nodes) {
      AnsibleClusterServerCtl.Params params = new AnsibleClusterServerCtl.Params();
      // Set the cloud name.
      params.cloud = CloudType.aws;
      // Add the node name.
      params.nodeName = node.instance_name;
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
  }

  /**
   * Create a task list to ping all servers until they are up.
   *
   * @param nodes : a collection of nodes that need to be pinged
   */
  public void createWaitForServerTasks(Collection<NodeDetails> nodes) {
    TaskList taskList = new TaskList("WaitForServer", executor);
    for (NodeDetails node : nodes) {
      WaitForServer.Params params = new WaitForServer.Params();
      params.universeUUID = taskParams().universeUUID;
      params.nodeName = node.instance_name;
      WaitForServer task = new WaitForServer();
      task.initialize(params);
      taskList.addTask(task);
    }
    taskListQueue.add(taskList);
  }

  public void createWaitForMasterLeaderTask() {
    TaskList taskList = new TaskList("WaitForMasterLeader", executor);
    WaitForMasterLeader task = new WaitForMasterLeader();
    WaitForMasterLeader.Params params = new WaitForMasterLeader.Params();
    params.universeUUID = taskParams().universeUUID;
    task.initialize(params);
    taskList.addTask(task);
    taskListQueue.add(taskList);
  }

  /**
   * Creates a task list to update the placement information by making a call to the master leader
   * of the cluster just created and adds it to the task queue.
   */
  public void createPlacementInfoTask(Collection<NodeDetails> blacklistNodes) {
    TaskList taskList = new TaskList("UpdatePlacementInfo", executor);
    UpdatePlacementInfo.Params params = new UpdatePlacementInfo.Params();
    // Set the cloud name.
    params.cloud = CloudType.aws;
    // Add the universe uuid.
    params.universeUUID = taskParams().universeUUID;
    // Set the blacklist nodes if any are passed in.
    if (blacklistNodes != null && !blacklistNodes.isEmpty()) {
      Set<String> blacklistNodeNames = new HashSet<String>();
      for (NodeDetails node : blacklistNodes) {
        blacklistNodeNames.add(node.instance_name);
      }
      params.blacklistNodes = blacklistNodeNames;
    }
    // Create the task to update placement info.
    UpdatePlacementInfo task = new UpdatePlacementInfo();
    task.initialize(params);
    // Add it to the task list.
    taskList.addTask(task);
    taskListQueue.add(taskList);
  }

  /**
   * Create a task to mark the change on a universe as success.
   */
  public void createMarkUniverseUpdateSuccessTasks() {
    TaskList taskList = new TaskList("FinalizeUniverseUpdate", executor);
    UniverseUpdateSucceeded.Params params = new UniverseUpdateSucceeded.Params();
    params.universeUUID = taskParams().universeUUID;
    UniverseUpdateSucceeded task = new UniverseUpdateSucceeded();
    task.initialize(params);
    taskList.addTask(task);
    taskListQueue.add(taskList);
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
