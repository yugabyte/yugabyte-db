// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.SubTaskGroup;
import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType;
import com.yugabyte.yw.commissioner.tasks.UpgradeUniverse.UpgradeTaskType;
import com.yugabyte.yw.commissioner.tasks.UpgradeUniverse.UpgradeTaskSubType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
import com.yugabyte.yw.commissioner.tasks.subtasks.ChangeMasterConfig;
import com.yugabyte.yw.commissioner.tasks.subtasks.ModifyBlackList;
import com.yugabyte.yw.commissioner.tasks.subtasks.WaitForDataMove;
import com.yugabyte.yw.commissioner.tasks.subtasks.WaitForLoadBalance;
import com.yugabyte.yw.common.DnsManager;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;

import java.util.Arrays;
import java.util.Collection;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import static com.yugabyte.yw.common.Util.areMastersUnderReplicated;

// Allows the addition of a node into a universe. Spawns the necessary processes - tserver
// and/or master and ensures the task waits for the right set of load balance primitives.
public class AddNodeToUniverse extends UniverseDefinitionTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(AddNodeToUniverse.class);

  @Override
  protected NodeTaskParams taskParams() {
    return (NodeTaskParams)taskParams;
  }

  @Override
  public void run() {
    LOG.info("Started {} task for node {} in univ uuid={}", getName(),
             taskParams().nodeName, taskParams().universeUUID);
    NodeDetails currentNode = null;
    boolean hitException = false;
    try {
      // Create the task list sequence.
      subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);

      // Update the DB to prevent other changes from happening.
      Universe universe = lockUniverseForUpdate(taskParams().expectedUniverseVersion);

      currentNode = universe.getNode(taskParams().nodeName);
      if (currentNode == null) {
        String msg = "No node " + taskParams().nodeName + " in universe " + universe.name;
        LOG.error(msg);
        throw new RuntimeException(msg);
      }

      if (currentNode.state != NodeDetails.NodeState.Removed &&
          currentNode.state != NodeDetails.NodeState.Decommissioned) {
        String msg = "Node " + taskParams().nodeName + " is not in removed or decommissioned state"
                     + ", but is in " + currentNode.state + ", so cannot be added.";
        LOG.error(msg);
        throw new RuntimeException(msg);
      }

      // Update Node State to being added.
      createSetNodeStateTask(currentNode, NodeState.Adding)
          .setSubTaskGroupType(SubTaskGroupType.StartingNode);

      Collection<NodeDetails> node = new HashSet<NodeDetails>(Arrays.asList(currentNode));

      // First spawn an instance for Decommissioned node.
      boolean wasDecommissioned = currentNode.state == NodeDetails.NodeState.Decommissioned;
      if (wasDecommissioned) {
        createSetupServerTasks(node)
            .setSubTaskGroupType(SubTaskGroupType.Provisioning);

        createServerInfoTasks(node)
            .setSubTaskGroupType(SubTaskGroupType.Provisioning);

        // Reset the current node info since it was respawned.
        currentNode = universe.getNode(taskParams().nodeName);
        node = new HashSet<NodeDetails>(Arrays.asList(currentNode));
      }

      // Bring up any masters, as needed.
      boolean masterAdded = false;
      if (areMastersUnderReplicated(currentNode, universe)) {
        // Configures the master to start in shell mode.
        // TODO: Remove the need for version for existing instance, NodeManger needs changes.
        createConfigureServerTasks(node, true /* isShell */)
            .setSubTaskGroupType(SubTaskGroupType.InstallingSoftware);

        // Start a shell master process.
        createStartMasterTasks(node)
            .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);

        // Mark node as a master in YW DB.
        // Do this last so that master addresses does not pick up current node.
        createUpdateNodeProcessTask(taskParams().nodeName, ServerType.MASTER, true)
            .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);

        // Wait for master to be responsive.
        createWaitForServersTasks(node, ServerType.MASTER)
            .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

        // Add it into the master quorum.
        createChangeConfigTask(currentNode, true, SubTaskGroupType.WaitForDataMigration);

        masterAdded = true;
      }

      // Configure so that this tserver picks all the master nodes.
      createConfigureServerTasks(node, false /* isShell */)
          .setSubTaskGroupType(SubTaskGroupType.InstallingSoftware);

      // Add the tserver process start task.
      createTServerTaskForNode(currentNode, "start")
          .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);

      // Mark the node as tserver in the YW DB.
      createUpdateNodeProcessTask(taskParams().nodeName, ServerType.TSERVER, true)
          .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);

      // Wait for new tablet servers to be responsive.
      createWaitForServersTasks(node, ServerType.TSERVER)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Wait for load to balance.
      createWaitForLoadBalanceTask()
          .setSubTaskGroupType(SubTaskGroupType.WaitForDataMigration);

      // Update all tserver conf files with new master information.
      if (masterAdded) {
        createMasterInfoUpdateTask(universe, currentNode);
      }

      // Update node state to live.
      createSetNodeStateTask(currentNode, NodeState.Live)
          .setSubTaskGroupType(SubTaskGroupType.StartingNode);

      if (wasDecommissioned) {
        UserIntent userIntent = universe.getUniverseDetails()
                                        .getClusterByUuid(currentNode.placementUuid)
                                        .userIntent;

        // Update the DNS entry for this universe.
        createDnsManipulationTask(DnsManager.DnsCommandType.Edit, false, userIntent.providerType,
                                  userIntent.provider, userIntent.universeName)
            .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      }

      // Mark universe task state to success.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.StartingNode);

      // Run all the tasks.
      subTaskGroupQueue.run();
    } catch (Throwable t) {
      LOG.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);
      hitException = true;
      throw t;
    } finally {
      // Reset the state, on any failure, so that the actions can be retried.
      if (currentNode != null && hitException) {
        setNodeState(taskParams().nodeName, currentNode.state);
      }

      // Mark the update of the universe as done. This will allow future updates to the universe.
      unlockUniverseForUpdate();
    }
    LOG.info("Finished {} task.", getName());
  }

  // Setup a configure task to update the new master list in the conf files of all tservers.
  // Skip the newly added node as it would have gotten the new master list after provisioing.
  private void createMasterInfoUpdateTask(Universe universe, NodeDetails skipNode) {
    List<NodeDetails> nodes = universe.getTServers();
    nodes.removeIf((NodeDetails node) ->
                    node.cloudInfo.private_ip.equals(skipNode.cloudInfo.private_ip));
    // Configure all tservers to pick the new master node ip as well.
    createConfigureServerTasks(nodes, false /* isShell */, true /* updateMasterAddr */)
        .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
  }
}
