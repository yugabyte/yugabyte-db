// Copyright (c) YugaByte, Inc.

package controllers.commissioner.tasks;

import java.util.ArrayList;
import java.util.Collection;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Set;
import java.util.TreeSet;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.ThreadFactory;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.util.concurrent.ThreadFactoryBuilder;

import controllers.commissioner.AbstractTaskBase;
import controllers.commissioner.Common.CloudType;
import controllers.commissioner.TaskList;
import controllers.commissioner.TaskListQueue;
import forms.commissioner.ITaskParams;
import forms.commissioner.UniverseTaskParams;
import models.commissioner.Universe;
import models.commissioner.Universe.NodeDetails;
import models.commissioner.Universe.UniverseDetails;
import models.commissioner.Universe.UniverseUpdater;
import play.libs.Json;

public abstract class UniverseTaskBase extends AbstractTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(UniverseTaskBase.class);

  // The default number of masters in the cluster.
  public static final int defaultNumMastersToChoose = 3;

  // This is the maximum number of subnets that the masters can be placed across, and need to be an
  // odd number for consensus to work.
  public static final int maxMasterSubnets = 3;

  @Override
  public void initialize(ITaskParams params) {
    this.taskParams = (UniverseTaskParams)params;
    // Create the threadpool for the subtasks to use.
    int numThreads = 10;
    ThreadFactory namedThreadFactory =
        new ThreadFactoryBuilder().setNameFormat("TaskPool-" + getName() + "-%d").build();
    executor = Executors.newFixedThreadPool(numThreads, namedThreadFactory);
  }

  // The task params.
  UniverseTaskParams taskParams;

  // The threadpool on which the tasks are executed.
  ExecutorService executor;

  // The sequence of task lists that should be executed.
  TaskListQueue taskListQueue;

  @Override
  public abstract void run();

  @Override
  public JsonNode getTaskDetails() {
    return Json.toJson(taskParams);
  }

  @Override
  public int getPercentCompleted() {
    if (taskListQueue == null) {
      return 0;
    }

    return taskListQueue.getPercentCompleted();
  }

  /**
   * Configures the set of nodes to be created and saves it to the universe info table.
   *
   * @param nodePrefix : the node name prefix to use
   * @param nodeStartIndex : id from which the new nodes should be numbered
   * @param numMasters : the number of masters desired in the edited universe
   * @param newNodesMap : out parameter, set of new nodes being created in the new universe
   * @param newMasters : out parameter, the subset of 'nodes' which are masters
   */
  public void configureNewNodes(String nodePrefix,
                                int nodeStartIndex,
                                int numMasters,
                                Map<String, NodeDetails> newNodesMap,
                                Set<NodeDetails> newMasters) {

    // Create the names and known properties of all the cluster nodes.
    for (int nodeIdx = nodeStartIndex; nodeIdx <= taskParams.numNodes; nodeIdx++) {
      NodeDetails nodeDetails = new NodeDetails();
      // Create the node name.
      nodeDetails.instance_name = nodePrefix + "-n" + nodeIdx;
      // Set the cloud name.
      nodeDetails.cloud = taskParams.cloudProvider;
      // Pick one of the VPCs in a round robin fashion.
      nodeDetails.subnet_id = taskParams.subnets.get(nodeIdx % taskParams.subnets.size());
      // Set the tablet server role to true.
      nodeDetails.isTserver = true;
      // Set the node id.
      nodeDetails.nodeIdx = nodeIdx;
      // Add the node to the list of nodes.
      newNodesMap.put(nodeDetails.instance_name, nodeDetails);
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
   * Saves the new values the universe should be modified to, as well as the 'updateInProgress'
   * flag. If the universe is already being modified, then throws an exception.
   */
  public Universe lockUniverseForUpdate() {
    // Create the update lambda.
    UniverseUpdater updater = new UniverseUpdater() {
      @Override
      public void run(Universe universe) {
        UniverseDetails universeDetails = universe.universeDetails;
        // If this universe is already being edited, fail the request.
        if (universeDetails.updateInProgress) {
          String msg = "Universe " + taskParams.universeUUID + " is already being updated.";
          LOG.error(msg);
          throw new RuntimeException(msg);
        }

        // Persist the updated information about the universe. Mark it as being edited.
        universeDetails.updateInProgress = true;
        universeDetails.updateSucceeded = false;
        universeDetails.nodePrefix = taskParams.nodePrefix;
        universeDetails.numNodes = taskParams.numNodes;
        universeDetails.subnets = taskParams.subnets;
        universeDetails.ybServerPkg = taskParams.ybServerPkg;
      }
    };
    // Perform the update. If unsuccessful, this will throw a runtime exception which we do not
    // catch as we want to fail.
    Universe universe = Universe.save(taskParams.universeUUID, updater);
    // Return the universe object that we have already updated.
    return universe;
  }

  public void unlockUniverseForUpdate() {
    // Create the update lambda.
    UniverseUpdater updater = new UniverseUpdater() {
      @Override
      public void run(Universe universe) {
        UniverseDetails universeDetails = universe.universeDetails;
        // If this universe is not being edited, fail the request.
        if (!universeDetails.updateInProgress) {
          String msg = "Universe " + taskParams.universeUUID + " is not being edited.";
          LOG.error(msg);
          throw new RuntimeException(msg);
        }
        // Persist the updated information about the universe. Mark it as being edited.
        universeDetails.updateInProgress = false;
      }
    };
    // Perform the update. If unsuccessful, this will throw a runtime exception which we do not
    // catch as we want to fail.
    Universe.save(taskParams.universeUUID, updater);
  }

  public void addNodesToUniverse(Collection<NodeDetails> nodes) {
    // Persist the desired node information into the DB.
    UniverseUpdater updater = new UniverseUpdater() {
      @Override
      public void run(Universe universe) {
        UniverseDetails universeDetails = universe.universeDetails;
        for (NodeDetails node : nodes) {
          // Since we have already set the 'updateInProgress' flag on this universe in the DB and
          // this step is single threaded, we are guaranteed no one else will be modifying it.
          // Replace the entire node value.
          universeDetails.nodeDetailsMap.put(node.instance_name, node);
        }
      }
    };
    Universe.save(taskParams.universeUUID, updater);
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
      // Add the node name.
      params.nodeName = node.instance_name;
      // Add the universe uuid.
      params.universeUUID = taskParams.universeUUID;
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
      // Add the node name.
      params.nodeName = node.instance_name;
      // Add the universe uuid.
      params.universeUUID = taskParams.universeUUID;
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
      params.universeUUID = taskParams.universeUUID;
      // The software package to install for this cluster.
      params.ybServerPkg = taskParams.ybServerPkg;
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
   * @param nodes : a collection of nodes that need to be created
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
      params.universeUUID = taskParams.universeUUID;
      // The service and the command we want to run.
      params.process = "master";
      params.command = isShell? "start" : "create";
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
      params.universeUUID = taskParams.universeUUID;
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
   * Creates a task list to update the placement information by making a call to the master leader
   * of the cluster just created and adds it to the task queue.
   */
  public void createPlacementInfoTask(Collection<NodeDetails> blacklistNodes) {
    TaskList taskList = new TaskList("UpdatePlacementInfo", executor);
    UpdatePlacementInfo.Params params = new UpdatePlacementInfo.Params();
    // Set the cloud name.
    params.cloud = CloudType.aws;
    // Add the universe uuid.
    params.universeUUID = taskParams.universeUUID;
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
   * Mark the universe as no longer being updated. This will allow other future edits to happen.
   */
  public void createMarkUniverseUpdateSuccessTasks() {
    TaskList taskList = new TaskList("FinalizeUniverseUpdate", executor);

    // TODO: fill this up.

    taskListQueue.add(taskList);
  }
}
