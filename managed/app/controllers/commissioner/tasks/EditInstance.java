// Copyright (c) YugaByte, Inc.

package controllers.commissioner.tasks;

import com.google.common.net.HostAndPort;
import java.util.ArrayList;
import java.util.Collection;
import java.util.List;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import controllers.commissioner.TaskListQueue;
import controllers.commissioner.TaskList;
import models.commissioner.InstanceInfo;
import util.Util;

// Tracks edit intents to the cluster and then performs the sequence of configuration changes on
// this instance to go from the current set of master/tserver nodes to the final configuration.
public class EditInstance extends InstanceTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(EditInstance.class);

  // Initial state of the host/ports in this instance, before editing it.
  List<HostAndPort> existingNodes;
  int numMasters = 0;

  @Override
  public String toString() {
    return getName() + "(" + taskParams.instanceUUID + ")";
  }

  @Override
  public String getName() {
    return "EditInstance";
  }

  @Override
  public void run() {
    LOG.info("Started {} task for uuid={}", getName(), taskParams.instanceUUID);
    // Create the task list sequence.
    taskListQueue = new TaskListQueue();

    try {
      // Check information about the instance.
      if (!InstanceInfo.exists(taskParams.instanceUUID)) {
        throw new IllegalArgumentException("The instance " + taskParams.instanceUUID +
            " does not exist for editing. Please create it first.");
      }

      // Bail if there is nothing to be changed between existing and target subnet lists. 
      if (!hasSubnetDifference()) {
        LOG.info("Nothing to modify for the instance {}.", taskParams.instanceUUID);
        return;
      }

      boolean isBeingUpdated = InstanceInfo.isBeingEdited(taskParams.instanceUUID);

      if (isBeingUpdated) {
        throw new IllegalArgumentException("The instance " + taskParams.instanceUUID +
            " is already being edited. Please wait for that to complete.");
      }

      existingNodes = Util.getHostPortList(InstanceInfo.getNodeDetails(taskParams.instanceUUID));
      if (existingNodes.isEmpty()) {
        throw new IllegalArgumentException("The instance " + taskParams.instanceUUID +
            " does not contain any nodes currently. Edit not allowed.");
      }

      numMasters = InstanceInfo.getMasters(taskParams.instanceUUID).size();

      // For now, we provision the same number of nodes as before.
      taskParams.numNodes = existingNodes.size();

      int startNodeIndex = InstanceInfo.getUseNodeIndex(taskParams.instanceUUID);

      LOG.info("Target cluster size = {}, numSubnets = {}, masters = {}, startIdx = {}",
               existingNodes.size(), taskParams.subnets.size(), numMasters, startNodeIndex);

      // Persist the edit intent information for this instance and the actual intent.
      InstanceInfo.setBeingEdited(taskParams.instanceUUID,
                                  taskParams.subnets,
                                  taskParams.numNodes,
                                  taskParams.ybServerPkg);

      // Create the required number of nodes in the appropriate locations.
      // Deploy the software on all the nodes.
      taskListQueue.add(createTaskListToSetupServers(startNodeIndex));

      // Get all information about the nodes of the cluster. This includes the public ip address,
      // the private ip address (in the case of AWS), etc.
      taskListQueue.add(createTaskListToGetServerInfo(startNodeIndex));

      // Pick the same number of masters as before and persist the plan in the middleware.
      taskListQueue.add(createTaskListToCreateClusterConf(numMasters));

      // Configures and deploys software on all the nodes (masters and tservers).
      taskListQueue.add(createTaskListToConfigureServers(startNodeIndex));

      // Creates the YB cluster by starting the masters in the shell mode.
      taskListQueue.add(createTaskListToCreateCluster(true));

      // Start the tservers in the clusters.
      taskListQueue.add(createTaskListToStartTServers(true));

      // Now finalize the cluster configuration change tasks. This adds directly to the global
      // list as only one change config job can be done in one step.
      setClusterConfigChangeTasks();

      // Persist the placement info and blacklisted node info into the YB master.
      // This is done after master config change jobs, so that the new master leader can perform
      // the auto load-balancing, and all tablet servers are heart beating to new set of masters.
      taskListQueue.add(createPlacementInfoTask(true));

      // Wait for %age completion of the tablet move from master.

      // TODO: Update the MetaMaster about the latest placement information.

      // Finally send destroy old set of nodes to ansible.

      // Atomic switch of instance new intent in instance info.
      taskListQueue.add(createTaskListToSwitchInstanceInfo());

      // Run all the tasks.
      taskListQueue.run();
    } catch (Throwable t) {
      LOG.error("Error executing task {} with error={}.", getName(), t);
      throw t;
    }
    LOG.info("Finished {} task.", getName());
  }

  private TaskList createTaskListToSwitchInstanceInfo() {
    TaskList taskList = new TaskList("UpdateInstanceInfo", executor);
    UpdateInstanceInfo uii = new UpdateInstanceInfo();
    UpdateInstanceInfo.Params params = new UpdateInstanceInfo.Params();   
    params.instanceUUID = taskParams.instanceUUID;
    params.isSuccess = false; // TODO: For now, just clear the intent
    uii.initialize(params);
    taskList.addTask(uii);
    return taskList;
  }

  // From the deleted subnet's, this api gets the list of masters running on those subnets.
  private List<HostAndPort> getNodesNotInTargetSubnets() {
    List<HostAndPort> retList = new ArrayList<HostAndPort>();
    List<String> existingSubnets = InstanceInfo.getSubnets(taskParams.instanceUUID);
    if (existingSubnets == null) {
      throw new IllegalStateException("No subnets found for " + taskParams.instanceUUID); 
    }
    List<String> subnetsNotInCurrent = Util.ListDiff(existingSubnets, taskParams.subnets);
    // Find the masters in this to-be-removed subnet list
    Collection<InstanceInfo.NodeDetails> nodes = InstanceInfo.getNodeDetails(taskParams.instanceUUID);
    LOG.info("Subnets cur={}, removed={}. Nodes={}", existingSubnets, subnetsNotInCurrent, nodes);
    for (InstanceInfo.NodeDetails node : nodes) {
      if (Util.existsInList(node.subnet_id, subnetsNotInCurrent)) {
         retList.add(HostAndPort.fromParts(node.public_ip, node.masterRpcPort));
      }
    }

    return retList;
  }

  // Fills in the series of steps needed to move the masters. The actual nodes to be added
  // are filled in during running of the change config tasks, by querying the instance info
  // database for the newly added nodes.
  private void setClusterConfigChangeTasks() {
    int numToBeAdded = taskParams.numNodes;
    List<HostAndPort> toBeDeleted = getNodesNotInTargetSubnets();
    int numIters = Math.min(numToBeAdded, toBeDeleted.size());
    LOG.info("Remove {} from existing nodes {} for a final size of {}.",
             toBeDeleted, existingNodes, numToBeAdded);

    // TODO: Find the master leader and remove it last. The step down will happen in client code.
    for (int i = 0; i < numIters; i++) {
      taskListQueue.add(createChangeConfigTask(i, true, null));
      taskListQueue.add(createChangeConfigTask(i, false, toBeDeleted));
    }
    for (int i = numIters; i < numToBeAdded; i++) {
      taskListQueue.add(createChangeConfigTask(i, true, null));
    }
    for (int i = numIters; i < toBeDeleted.size(); i++) {
      taskListQueue.add(createChangeConfigTask(i, false, toBeDeleted));
    }
    LOG.info("Change Creation creation done");
  }

  private TaskList createChangeConfigTask(int index, boolean isAdd, List<HostAndPort> toBeDeleted) {
    TaskList taskList = new TaskList("ChangeMasterConfig:" + index + "-" + isAdd, executor);
    HostAndPort hp = isAdd ? null : toBeDeleted.get(index);
    ChangeMasterConfig.Params params = ChangeMasterConfig.getParams(hp, isAdd);
    params.instanceUUID = taskParams.instanceUUID;
    ChangeMasterConfig changeConfig = new ChangeMasterConfig();
    changeConfig.initialize(params);
    taskList.addTask(changeConfig);
    return taskList;
  }

  // Helper method to check if there is any difference in the subnet lists.
  private boolean hasSubnetDifference() {
    List<String> existingSubnets = InstanceInfo.getSubnets(taskParams.instanceUUID);
    if (existingSubnets == null) {
      throw new IllegalStateException("No subnets found for " + taskParams.instanceUUID); 
    }

    taskParams.subnets = Util.ListDiff(taskParams.subnets, existingSubnets);
    List<String> subnetsNotInCurrent = Util.ListDiff(existingSubnets, taskParams.subnets);

    LOG.info("Subnets: size={}, notInCur={}.", taskParams.subnets.size(), subnetsNotInCurrent.size());

    if (taskParams.subnets.size() != 0 || subnetsNotInCurrent.size() != 0) {
      return true;
    }

    return false;
  }
}
