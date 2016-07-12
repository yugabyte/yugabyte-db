// Copyright (c) YugaByte, Inc.

package controllers.commissioner.tasks;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.TreeSet;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import controllers.commissioner.TaskListQueue;
import models.commissioner.InstanceInfo;
import models.commissioner.InstanceInfo.NodeDetails;

public class CreateInstance extends InstanceTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(CreateInstance.class);

  // The default number of masters in the cluster.
  public static final int defaultNumMastersToChoose = 3;

  // This is the maximum number of subnets that the masters can be placed across, and need to be an
  // odd number for consensus to work.
  public static final int maxMasterSubnets = 3;

  // The set of new nodes that need to be created.
  Map<String, NodeDetails> newNodesMap = new HashMap<String, NodeDetails>();

  @Override
  public String toString() {
    return getName() + "(" + taskParams.instanceUUID + ")";
  }

  @Override
  public String getName() {
    return "CreateInstance";
  }

  @Override
  public void run() {
    LOG.info("Started {} task.", getName());
    try {
      // Create the task list sequence.
      taskListQueue = new TaskListQueue();

      // Persist information about the instance.
      InstanceInfo.createInstance(taskParams.instanceUUID,
                                  taskParams.subnets,
                                  taskParams.numNodes,
                                  taskParams.ybServerPkg);

      // Configure the cluster nodes and persist that information in the db.
      configureNewNodes();

      // Create the required number of nodes in the appropriate locations.
      taskListQueue.add(createTaskListToSetupServers(1 /* startIndex */));

      // Get all information about the nodes of the cluster. This includes the public ip address,
      // the private ip address (in the case of AWS), etc.
      taskListQueue.add(createTaskListToGetServerInfo(1 /* startIndex */));

      // Configures and deploys software on all the nodes (masters and tservers).
      taskListQueue.add(createTaskListToConfigureServers(1 /* startIndex */));

      // Creates the YB cluster by starting the masters in the create mode.
      taskListQueue.add(createTaskListToCreateCluster(false /* isShell */));

      // Persist the placement info into the YB master.
      taskListQueue.add(createPlacementInfoTask(false /* isShell */));

      // Start the tservers in the clusters.
      taskListQueue.add(createTaskListToStartTServers(false /* isEdit */));

      // TODO: Update the MetaMaster about the latest placement information.

      // Run all the tasks.
      taskListQueue.run();
    } catch (Throwable t) {
      LOG.error("Error executing task {}, error={}", getName(), t);
      throw t;
    }
    LOG.info("Finished {} task.", getName());
  }

  /**
   * Configures the set of new nodes to be created.
   */
  public void configureNewNodes() {
    int numMastersToChoose = defaultNumMastersToChoose;
    int startIndex = 1;

    // Create the names and known properties of all the cluster nodes.
    for (int nodeIdx = startIndex; nodeIdx <= taskParams.numNodes; nodeIdx++) {
      NodeDetails nodeDetails = new NodeDetails();
      // Create the node instance name.
      nodeDetails.instance_name = taskParams.instanceName + "-n" + nodeIdx;
      // Set the cloud name.
      nodeDetails.cloud = taskParams.cloudProvider;
      // Pick one of the VPCs in a round robin fashion.
      nodeDetails.subnet_id = taskParams.subnets.get(nodeIdx % taskParams.subnets.size());
      // Mark this node as being setup.
      nodeDetails.isBeingSetup = true;
      // Set the tablet server role to true.
      nodeDetails.isTserver = true;
      // Add the node to the list of nodes.
      newNodesMap.put(nodeDetails.instance_name, nodeDetails);
    }

    // Group the nodes by subnets.
    Map<String, TreeSet<String>> subnetsToNodenameMap = groupBySubnets(newNodesMap);

    // Choose the masters such that we have one master per subnet.
    List<String> masters = new ArrayList<String>();
    if (subnetsToNodenameMap.size() >= maxMasterSubnets) {
      for (Entry<String, TreeSet<String>> entry : subnetsToNodenameMap.entrySet()) {
        // Get one node from each subnet.
        String nodeName = entry.getValue().first();
        newNodesMap.get(nodeName).isMaster = true;
        masters.add(nodeName);
        LOG.info("Chose node {} as a master from subnet {}.", nodeName, entry.getKey());
        if (masters.size() == numMastersToChoose) {
          break;
        }
      }
    } else {
      for (NodeDetails node : newNodesMap.values()) {
        newNodesMap.get(node.instance_name).isMaster = true;
        masters.add(node.instance_name);
        LOG.info("Chose node {} as a master from subnet {}.", node.instance_name, node.subnet_id);
        if (masters.size() == numMastersToChoose) {
          break;
        }
      }
    }

    // Persist the desired node information.
    for (Entry<String, NodeDetails> entry : newNodesMap.entrySet()) {
      InstanceInfo.updateNodeDetails(taskParams.instanceUUID, entry.getKey(), entry.getValue());
    }
  }

  public static Map<String, TreeSet<String>> groupBySubnets(Map<String, NodeDetails> nodesMap) {
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
    return subnetsToNodenameMap;
  }
}
