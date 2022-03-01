/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.common.Util.areMastersUnderReplicated;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.certmgmt.EncryptionInTransitUtil;
import com.yugabyte.yw.common.DnsManager;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

// Allows the addition of a node into a universe. Spawns the necessary processes - tserver
// and/or master and ensures the task waits for the right set of load balance primitives.
@Slf4j
public class AddNodeToUniverse extends UniverseDefinitionTaskBase {

  @Inject
  protected AddNodeToUniverse(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected NodeTaskParams taskParams() {
    return (NodeTaskParams) taskParams;
  }

  @Override
  public void run() {
    log.info(
        "Started {} task for node {} in univ uuid={}",
        getName(),
        taskParams().nodeName,
        taskParams().universeUUID);
    NodeDetails currentNode = null;
    String errorString = null;

    try {
      checkUniverseVersion();
      // Create the task list sequence.
      subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);

      // Update the DB to prevent other changes from happening.
      Universe universe = lockUniverseForUpdate(taskParams().expectedUniverseVersion);

      currentNode = universe.getNode(taskParams().nodeName);
      if (currentNode == null) {
        String msg = "No node " + taskParams().nodeName + " in universe " + universe.name;
        log.error(msg);
        throw new RuntimeException(msg);
      }

      if (currentNode.state != NodeState.Removed && currentNode.state != NodeState.Decommissioned) {
        String msg =
            "Node "
                + taskParams().nodeName
                + " is not in removed or decommissioned state"
                + ", but is in "
                + currentNode.state
                + ", so cannot be added.";
        log.error(msg);
        throw new RuntimeException(msg);
      }

      preTaskActions();

      Cluster cluster = taskParams().getClusterByUuid(currentNode.placementUuid);
      UserIntent userIntent = cluster.userIntent;
      Collection<NodeDetails> node = Collections.singletonList(currentNode);

      boolean wasDecommissioned = currentNode.state == NodeState.Decommissioned;
      // For onprem universes, allocate an available node
      // from the provider's node_instance table.
      if (wasDecommissioned && cluster.userIntent.providerType.equals(CloudType.onprem)) {
        Map<UUID, List<String>> onpremAzToNodes = new HashMap<UUID, List<String>>();
        List<String> nodeNameList = new ArrayList<>();
        nodeNameList.add(currentNode.nodeName);
        onpremAzToNodes.put(currentNode.azUuid, nodeNameList);
        String instanceType = currentNode.cloudInfo.instance_type;

        Map<String, NodeInstance> nodeMap = NodeInstance.pickNodes(onpremAzToNodes, instanceType);
        currentNode.nodeUuid = nodeMap.get(currentNode.nodeName).getNodeUuid();
      }

      String preflightStatus = null;
      // Perform preflight check for onprem cluster
      if (cluster.userIntent.providerType == CloudType.onprem) {
        preflightStatus =
            performPreflightCheck(
                cluster,
                currentNode,
                EncryptionInTransitUtil.isRootCARequired(taskParams()) ? taskParams().rootCA : null,
                EncryptionInTransitUtil.isClientRootCARequired(taskParams())
                    ? taskParams().clientRootCA
                    : null);
      }

      if (preflightStatus != null) {
        Map<String, String> failedNodes =
            Collections.singletonMap(currentNode.nodeName, preflightStatus);
        createFailedPrecheckTask(failedNodes, true)
            .setSubTaskGroupType(SubTaskGroupType.PreflightChecks);
        errorString = "Preflight checks failed.";
      } else {
        // Update Node State to being added.
        createSetNodeStateTask(currentNode, NodeState.Adding)
            .setSubTaskGroupType(SubTaskGroupType.StartingNode);

        // First spawn an instance for Decommissioned node.
        if (wasDecommissioned) {
          createCreateServerTasks(node).setSubTaskGroupType(SubTaskGroupType.Provisioning);

          createServerInfoTasks(node).setSubTaskGroupType(SubTaskGroupType.Provisioning);

          createSetupServerTasks(node).setSubTaskGroupType(SubTaskGroupType.Provisioning);
        }

        // Re-install software.
        // TODO: Remove the need for version for existing instance, NodeManger needs changes.
        createConfigureServerTasks(node, true /* isShell */)
            .setSubTaskGroupType(SubTaskGroupType.InstallingSoftware);

        // All necessary nodes are created. Data moving will coming soon.
        createSetNodeStateTasks(node, NodeDetails.NodeState.ToJoinCluster)
            .setSubTaskGroupType(SubTaskGroupType.Provisioning);

        // Bring up any masters, as needed.
        boolean masterAdded = false;
        if (areMastersUnderReplicated(currentNode, universe)) {
          log.info(
              "Bringing up master for under replicated universe {} ({})",
              universe.universeUUID,
              universe.name);

          // Set gflags for master.
          createGFlagsOverrideTasks(node, ServerType.MASTER, true /* isShell */);

          // Start a shell master process.
          createStartMasterTasks(node).setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);

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

        // Set gflags for the tserver.
        createGFlagsOverrideTasks(node, ServerType.TSERVER);

        // Add the tserver process start task.
        createTServerTaskForNode(currentNode, "start")
            .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);

        // Mark the node as tserver in the YW DB.
        createUpdateNodeProcessTask(taskParams().nodeName, ServerType.TSERVER, true)
            .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);

        // Wait for new tablet servers to be responsive.
        createWaitForServersTasks(node, ServerType.TSERVER)
            .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

        // Update the swamper target file.
        createSwamperTargetUpdateTask(false /* removeFile */);

        // Clear the host from master's blacklist.
        if (currentNode.state == NodeState.Removed) {
          createModifyBlackListTask(
                  Arrays.asList(currentNode), false /* isAdd */, false /* isLeaderBlacklist */)
              .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
        }

        // Wait for load to balance.
        createWaitForLoadBalanceTask().setSubTaskGroupType(SubTaskGroupType.WaitForDataMigration);

        // Update all tserver conf files with new master information.
        if (masterAdded) {
          createMasterInfoUpdateTask(universe, currentNode);
        }

        // Update node state to live.
        createSetNodeStateTask(currentNode, NodeState.Live)
            .setSubTaskGroupType(SubTaskGroupType.StartingNode);

        // Update the DNS entry for this universe.
        createDnsManipulationTask(DnsManager.DnsCommandType.Edit, false, userIntent)
            .setSubTaskGroupType(SubTaskGroupType.StartingNode);

        // Mark universe task state to success.
        createMarkUniverseUpdateSuccessTasks().setSubTaskGroupType(SubTaskGroupType.StartingNode);
      }

      // Run all the tasks.
      subTaskGroupQueue.run();
    } catch (Throwable t) {
      log.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);
      throw t;
    } finally {
      // Mark the update of the universe as done. This will allow future updates to the universe.
      unlockUniverseForUpdate(errorString);
    }
    log.info("Finished {} task.", getName());
  }
}
