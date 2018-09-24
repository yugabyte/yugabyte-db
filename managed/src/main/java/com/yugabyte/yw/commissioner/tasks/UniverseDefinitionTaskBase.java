// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import java.util.ArrayList;
import java.util.Collection;
import java.util.HashSet;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;

import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesWaitForPod;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.SubTaskGroup;
import com.yugabyte.yw.commissioner.tasks.UpgradeUniverse.UpgradeTaskType;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleClusterServerCtl;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleSetupServer;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleUpdateNodeInfo;
import com.yugabyte.yw.commissioner.tasks.subtasks.UpdatePlacementInfo;
import com.yugabyte.yw.commissioner.tasks.subtasks.WaitForMasterLeader;
import com.yugabyte.yw.commissioner.tasks.subtasks.WaitForTServerHeartBeats;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;

/**
 * Abstract base class for all tasks that create/edit the universe definition. These include the
 * create universe task and all forms of edit universe tasks. Note that the delete universe task
 * extends the UniverseTaskBase, as it does not depend on the universe definition.
 */
public abstract class UniverseDefinitionTaskBase extends UniverseTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(UniverseDefinitionTaskBase.class);

  // Enum for specifying the server type.
  public enum ServerType {
    MASTER,
    TSERVER,
    YQLSERVER,
    REDISSERVER,
    EITHER
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
    return writeUserIntentToUniverse(false);
  }

  /**
   * Writes the user intent to the universe.
   * @param isReadOnlyCreate only readonly cluster being created info needs peristence.
   */
  public Universe writeUserIntentToUniverse(boolean isReadOnlyCreate) {
    // Create the update lambda.
    UniverseUpdater updater = new UniverseUpdater() {
      @Override
      public void run(Universe universe) {
        // Persist the updated information about the universe.
        // It should have been marked as being edited in lockUniverseForUpdate().
        UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
        if (!universeDetails.updateInProgress) {
          String msg = "Universe " + taskParams().universeUUID +
                       " has not been marked as being updated.";
          LOG.error(msg);
          throw new RuntimeException(msg);
        }
        if (!isReadOnlyCreate) {
          universeDetails.nodeDetailsSet = taskParams().nodeDetailsSet;
          universeDetails.nodePrefix = taskParams().nodePrefix;
          universeDetails.universeUUID = taskParams().universeUUID;
          Cluster cluster = taskParams().getPrimaryCluster();
          if (cluster != null) {
            universeDetails.upsertPrimaryCluster(cluster.userIntent, cluster.placementInfo);
          } // else read only cluster edit mode.
        } else {
          // Combine the existing nodes with new read only cluster nodes.
          universeDetails.nodeDetailsSet.addAll(taskParams().nodeDetailsSet);
        }
        taskParams().getReadOnlyClusters().stream().forEach((async) -> {
          universeDetails.upsertCluster(async.userIntent, async.placementInfo, async.uuid);
        });
        universe.setUniverseDetails(universeDetails);
      }
    };
    // Perform the update. If unsuccessful, this will throw a runtime exception which we do not
    // catch as we want to fail.
    Universe universe = Universe.saveDetails(taskParams().universeUUID, updater);
    LOG.debug("Wrote user intent for universe {}.", taskParams().universeUUID);
    // Return the universe object that we have already updated.
    return universe;
  }

  /**
   * Delete a cluster from the universe.
   * @param clusterUUID uuid of the cluster user wants to delete.
   */
  public void deleteClusterFromUniverse(UUID clusterUUID) {
    UniverseUpdater updater = new UniverseUpdater() {
      @Override
      public void run(Universe universe) {
        UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
        universeDetails.deleteCluster(clusterUUID);
        universe.setUniverseDetails(universeDetails);
      }
    };
    Universe.saveDetails(taskParams().universeUUID, updater);
    LOG.info("Delete cluster {} done.", clusterUUID);
  }

  // Helper data structure to save the new name and index of nodes for quick lookup using the
  // old name of nodes.
  private class NameAndIndex {
    String name;
    int index;

    public NameAndIndex(String name, int index) {
      this.name = name;
      this.index = index;
    }
    
    public String toString() {
      return "{name: " + name + ", index: " + index + "}";
    }
  }

  // The universe name can be changed in the UI, and say if configure is not called
  // before submitting Create, we need to fix up the node-prefix also to latest universe name.
  private String updateUniverseName(Universe universe) {
    final String univNewName = taskParams().getPrimaryCluster().userIntent.universeName;
    final boolean univNameChanged = !universe.name.equals(univNewName);
    String nodePrefix = taskParams().nodePrefix;

    // Pick the universe name from the current in-memory state.
    // Note that `universe` should have the new name persisted before this call.
    if (!nodePrefix.contains(univNewName)) {
      if (univNameChanged) {
        LOG.warn("Universe name mismatched: expected {} but found {}. Updating to {}.",
                 univNewName, universe.name, univNewName);
      }
      nodePrefix = Util.getNodePrefix(universe.customerId, univNewName);
      LOG.info("Updating node prefix to {}.", nodePrefix);
    }

    // Persist the desired node information into the DB.
    UniverseUpdater updater = new UniverseUpdater() {
      @Override
      public void run(Universe universe) {
        if (univNameChanged) {
          universe.name = univNewName;
        }
      }
    };
    universe = Universe.saveDetails(taskParams().universeUUID, updater);

    return nodePrefix;
  }

  // Fix up names of all the nodes. This fixes the name and the node index for being created nodes.
  public void updateNodeNames() {
    PlacementInfoUtil.populateClusterIndices(taskParams());
    Universe universe = Universe.get(taskParams().universeUUID);
    final Map<String, NameAndIndex> oldToNewName = new HashMap<String, NameAndIndex>();
    String nodePrefix = taskParams().nodePrefix;

    // Check if we need to change the universe name, only when creating a universe - when task
    // contains a primary cluster.
    if (taskParams().getPrimaryCluster() != null) {
      nodePrefix = updateUniverseName(universe);
    }

    for (Cluster cluster : taskParams().clusters) {
      Set<NodeDetails> nodesInClusterTask = taskParams().getNodesInCluster(cluster.uuid);
      int startIndex = PlacementInfoUtil.getStartIndex(
          universe.getUniverseDetails().getNodesInCluster(cluster.uuid));
      int iter = 0;
      for (NodeDetails node : nodesInClusterTask) {
        if (node.state == NodeDetails.NodeState.ToBeAdded) {
          node.nodeIdx = startIndex + iter;
          String newName = nodePrefix + "-n" + node.nodeIdx;
          if (cluster.clusterType == ClusterType.ASYNC) {
            newName = nodePrefix + Universe.READONLY + cluster.index + Universe.NODEIDX_PREFIX +
                      node.nodeIdx;
          }
          LOG.info("Changing in-memory node name from {} to {}.", node.nodeName , newName);
          oldToNewName.put(node.nodeName, new NameAndIndex(newName, node.nodeIdx));
          node.nodeName = newName;
          iter++;
        }
      }
    }

    PlacementInfoUtil.ensureUniqueNodeNames(taskParams().nodeDetailsSet);

    // Persist the desired node information into the DB.
    UniverseUpdater updater = new UniverseUpdater() {
      @Override
      public void run(Universe universe) {
        Collection<NodeDetails> univNodes = universe.getNodes();
        for (NodeDetails node : univNodes) {
          if (node.state == NodeDetails.NodeState.ToBeAdded) {
            // Since we have already set the 'updateInProgress' flag on this universe in the DB and
            // this step is single threaded, we are guaranteed no one else will be modifying it.
            NameAndIndex newInfo = oldToNewName.get(node.nodeName);
            LOG.info("Changing node name from {} to newInfo={}.", node.nodeName, newInfo);
            node.nodeName = newInfo.name;
            node.nodeIdx = newInfo.index;
          }
        }
      }
    };
    universe = Universe.saveDetails(taskParams().universeUUID, updater);
    LOG.debug("Updated {} nodes in universe {}.", taskParams().nodeDetailsSet.size(),
              taskParams().universeUUID);

    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();

    List<Cluster> onPremClusters = universeDetails.clusters.stream()
            .filter(c -> c.userIntent.providerType.equals(CloudType.onprem))
            .collect(Collectors.toList());
    for (Cluster onPremCluster : onPremClusters) {
      Map<UUID, List<String>> onpremAzToNodes = new HashMap<UUID, List<String>>();
      for (NodeDetails node : universeDetails.getNodesInCluster(onPremCluster.uuid)) {
        if (node.state == NodeDetails.NodeState.ToBeAdded) {
          List<String> nodeNames = onpremAzToNodes.getOrDefault(node.azUuid, new ArrayList<String>());
          nodeNames.add(node.nodeName);
          onpremAzToNodes.put(node.azUuid, nodeNames);
        }
      }
      // Update in-memory map.
      String instanceType = onPremCluster.userIntent.instanceType;
      Map<String, NodeInstance> nodeMap = NodeInstance.pickNodes(onpremAzToNodes, instanceType);
      for (NodeDetails node : taskParams().nodeDetailsSet) {
        // TODO: use the UUID to select the node, but this requires a refactor of the tasks/params
        // to more easily trickle down this uuid into all locations.
        NodeInstance n = nodeMap.get(node.nodeName);
        if (n != null) {
          node.nodeUuid = n.nodeUuid;
        }
      }
    }
  }

  public void createGFlagsOverrideTasks(Collection<NodeDetails> nodes, ServerType taskType) {
    // Skip if no extra flags for MASTER in primary cluster.
    if (taskType.equals(ServerType.MASTER) &&
        (taskParams().getPrimaryCluster() == null ||
         taskParams().getPrimaryCluster().userIntent.masterGFlags.isEmpty())) {
      return;
    }

    // Skip if all clusters have no extra TSERVER flags. (No cluster has an extra TSERVER flag.)
    if (taskType.equals(ServerType.TSERVER) &&
        taskParams().clusters.stream().allMatch(c -> c.userIntent.tserverGFlags.isEmpty())) {
      return;
    }

    SubTaskGroup subTaskGroup = new SubTaskGroup("AnsibleConfigureServersGFlags", executor);
    for (NodeDetails node : nodes) {
      UserIntent userIntent = taskParams().getClusterByUuid(node.placementUuid).userIntent;
      Map<String, String> gflags = taskType.equals(ServerType.MASTER) ? userIntent.masterGFlags
                                                                      : userIntent.tserverGFlags;
      if (gflags == null || gflags.isEmpty()) {
        continue;
      }

      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      // Set the device information (numVolumes, volumeSize, etc.)
      params.deviceInfo = userIntent.deviceInfo;
      // Add the node name.
      params.nodeName = node.nodeName;
      // Add the universe uuid.
      params.universeUUID = taskParams().universeUUID;
      // Add the az uuid.
      params.azUuid = node.azUuid;
      params.placementUuid = node.placementUuid;
      // Add task type
      params.type = UpgradeUniverse.UpgradeTaskType.GFlags;
      params.setProperty("processType", taskType.toString());
      params.gflags = gflags;
      AnsibleConfigureServers task = new AnsibleConfigureServers();
      task.initialize(params);
      subTaskGroup.addTask(task);
    }

    if (subTaskGroup.getNumTasks() == 0) {
      return;
    }

    subTaskGroup.setSubTaskGroupType(SubTaskGroupType.UpdatingGFlags);
    subTaskGroupQueue.add(subTaskGroup);
  }

  /**
   * Creates a task list to start the tservers on the set of passed in nodes and adds it to the task
   * queue.
   *
   * @param nodes : a collection of nodes that need to be created
   */
  public SubTaskGroup createStartTServersTasks(Collection<NodeDetails> nodes) {
    SubTaskGroup subTaskGroup = new SubTaskGroup("AnsibleClusterServerCtl", executor);
    for (NodeDetails node : nodes) {
      AnsibleClusterServerCtl.Params params = new AnsibleClusterServerCtl.Params();
      // Add the node name.
      params.nodeName = node.nodeName;
      // Add the universe uuid.
      params.universeUUID = taskParams().universeUUID;
      // Add the az uuid.
      params.azUuid = node.azUuid;
      // The service and the command we want to run.
      params.process = "tserver";
      params.command = "start";
      params.placementUuid = node.placementUuid;
      // Set the InstanceType
      params.instanceType = node.cloudInfo.instance_type;
      // Create the Ansible task to get the server info.
      AnsibleClusterServerCtl task = new AnsibleClusterServerCtl();
      task.initialize(params);
      // Add it to the task list.
      subTaskGroup.addTask(task);
    }
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createWaitForMasterLeaderTask() {
    SubTaskGroup subTaskGroup = new SubTaskGroup("WaitForMasterLeader", executor);
    WaitForMasterLeader task = new WaitForMasterLeader();
    WaitForMasterLeader.Params params = new WaitForMasterLeader.Params();
    params.universeUUID = taskParams().universeUUID;
    task.initialize(params);
    subTaskGroup.addTask(task);
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Creates a task list to wait for a minimum number of tservers to heartbeat
   * to the master leader.
   */
  public SubTaskGroup createWaitForTServerHeartBeatsTask() {
    SubTaskGroup subTaskGroup = new SubTaskGroup("WaitForTServerHeartBeats", executor);
    WaitForTServerHeartBeats task = new WaitForTServerHeartBeats();
    WaitForTServerHeartBeats.Params params = new WaitForTServerHeartBeats.Params();
    params.universeUUID = taskParams().universeUUID;
    task.initialize(params);
    subTaskGroup.addTask(task);
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Creates a task list for provisioning the list of nodes passed in and adds it to the task queue.
   *
   * @param nodes : a collection of nodes that need to be created
   */
  public SubTaskGroup createSetupServerTasks(Collection<NodeDetails> nodes) {
    SubTaskGroup subTaskGroup = new SubTaskGroup("AnsibleSetupServer", executor);

    for (NodeDetails node : nodes) {
      UserIntent userIntent = taskParams().getClusterByUuid(node.placementUuid).userIntent;
      AnsibleSetupServer.Params params = new AnsibleSetupServer.Params();
      // Set the device information (numVolumes, volumeSize, etc.)
      CloudSpecificInfo cloudInfo = node.cloudInfo;
      params.deviceInfo = userIntent.deviceInfo;
      // Set the region code.
      params.azUuid = node.azUuid;
      params.placementUuid = node.placementUuid;
      // Add the node name.
      params.nodeName = node.nodeName;
      // Add the universe uuid.
      params.universeUUID = taskParams().universeUUID;
      // Pick one of the subnets in a round robin fashion.
      params.subnetId = cloudInfo.subnet_id;
      // Set the instance type.
      params.instanceType = cloudInfo.instance_type;
      // Set the spot price.
      params.spotPrice = cloudInfo.spotPrice;
      // Set the assign public ip param.
      params.assignPublicIP = cloudInfo.assignPublicIP;
      params.useTimeSync = cloudInfo.useTimeSync;
      // Create the Ansible task to setup the server.
      AnsibleSetupServer ansibleSetupServer = new AnsibleSetupServer();
      ansibleSetupServer.initialize(params);
      // Add it to the task list.
      subTaskGroup.addTask(ansibleSetupServer);
    }
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Creates a task list to configure the newly provisioned nodes and adds it to the task queue.
   * Includes tasks such as setting up the 'yugabyte' user and installing the passed in software
   * package.
   *
   * @param nodes : a collection of nodes that need to be created
   * @param isMasterInShellMode : true if we are configuring a master node in shell mode
   * @param updateMasterAddrsOnly if true, only want to set master addresses gflags.
   * @return subtask group
   */
  public SubTaskGroup createConfigureServerTasks(Collection<NodeDetails> nodes,
                                                 boolean isMasterInShellMode) {
    return createConfigureServerTasks(nodes, isMasterInShellMode, false /* updateMasterAddrs */);
  }

  public SubTaskGroup createConfigureServerTasks(Collection<NodeDetails> nodes,
                                                 boolean isMasterInShellMode,
                                                 boolean updateMasterAddrsOnly) {
    SubTaskGroup subTaskGroup = new SubTaskGroup("AnsibleConfigureServers", executor);
    for (NodeDetails node : nodes) {
      UserIntent userIntent = taskParams().getClusterByUuid(node.placementUuid).userIntent;
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      // Set the device information (numVolumes, volumeSize, etc.)
      params.deviceInfo = userIntent.deviceInfo;
      // Add the node name.
      params.nodeName = node.nodeName;
      // Add the universe uuid.
      params.universeUUID = taskParams().universeUUID;
      // Add the az uuid.
      params.azUuid = node.azUuid;
      params.placementUuid = node.placementUuid;
      // Sets the isMaster field
      params.isMaster = node.isMaster;
      // Set if this node is a master in shell mode.
      params.isMasterInShellMode = isMasterInShellMode;
      // The software package to install for this cluster.
      params.ybSoftwareVersion = userIntent.ybSoftwareVersion;
      // Set the InstanceType
      params.instanceType = node.cloudInfo.instance_type;
      // Set if updating master addresses only.
      params.updateMasterAddrsOnly = updateMasterAddrsOnly;
      if (updateMasterAddrsOnly) {
        params.type = UpgradeTaskType.GFlags;
        params.setProperty("processType", ServerType.TSERVER.toString());
      }
      // Create the Ansible task to get the server info.
      AnsibleConfigureServers task = new AnsibleConfigureServers();
      task.initialize(params);
      // Add it to the task list.
      subTaskGroup.addTask(task);
    }
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Creates a task list for fetching information about the nodes provisioned (such as the ip
   * address) and adds it to the task queue. This is specific to the cloud.
   *
   * @param nodes : a collection of nodes that need to be provisioned
   * @return subtask group
   */
  public SubTaskGroup createServerInfoTasks(Collection<NodeDetails> nodes) {
    SubTaskGroup subTaskGroup = new SubTaskGroup("AnsibleUpdateNodeInfo", executor);

    for (NodeDetails node : nodes) {
      NodeTaskParams params = new NodeTaskParams();
      UserIntent userIntent = taskParams().getClusterByUuid(node.placementUuid).userIntent;
      // Set the device information (numVolumes, volumeSize, etc.)
      params.deviceInfo = userIntent.deviceInfo;
      // Set the region name to the proper provider code so we can use it in the cloud API calls.
      params.azUuid = node.azUuid;
      params.placementUuid = node.placementUuid;
      // Add the node name.
      params.nodeName = node.nodeName;
      // Add the universe uuid.
      params.universeUUID = taskParams().universeUUID;
      // Create the Ansible task to get the server info.
      AnsibleUpdateNodeInfo ansibleFindCloudHost = new AnsibleUpdateNodeInfo();
      ansibleFindCloudHost.initialize(params);
      // Add it to the task list.
      subTaskGroup.addTask(ansibleFindCloudHost);
    }
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
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
    for (Cluster cluster : taskParams().clusters) {
      PlacementInfoUtil.verifyNodesAndRF(cluster.clusterType, cluster.userIntent.numNodes,
                                         cluster.userIntent.replicationFactor);
    }
  }

  public void createKubernetesExecutorTask(KubernetesCommandExecutor.CommandType commandType) {
    createKubernetesExecutorTask(commandType, null);    
  }

  public void createKubernetesExecutorTask(KubernetesCommandExecutor.CommandType commandType, String ybSoftwareVersion) {
    SubTaskGroup subTaskGroup = new SubTaskGroup(commandType.getSubTaskGroupName(), executor);
    KubernetesCommandExecutor.Params params = new KubernetesCommandExecutor.Params();
    UniverseDefinitionTaskParams.Cluster primary = taskParams().getPrimaryCluster();
    params.providerUUID = UUID.fromString(
        primary.userIntent.provider);
    params.commandType = commandType;
    params.nodePrefix = taskParams().nodePrefix;
    params.universeUUID = taskParams().universeUUID;
    if (ybSoftwareVersion != null) {
      params.ybSoftwareVersion = ybSoftwareVersion;
    }
    KubernetesCommandExecutor task = new KubernetesCommandExecutor();
    task.initialize(params);
    subTaskGroup.addTask(task);
    subTaskGroupQueue.add(subTaskGroup);
    subTaskGroup.setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.Provisioning);
  }
}
