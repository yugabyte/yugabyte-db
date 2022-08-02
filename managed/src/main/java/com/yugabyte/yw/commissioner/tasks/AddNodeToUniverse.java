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

import static com.google.api.client.util.Preconditions.checkState;
import static com.yugabyte.yw.common.Util.areMastersUnderReplicated;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.certmgmt.EncryptionInTransitUtil;
import com.yugabyte.yw.common.DnsManager;
import com.yugabyte.yw.common.NodeActionType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.VMImageUpgradeParams.VmUpgradeTaskType;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
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
        "Started {} task for node {} in universe {}",
        getName(),
        taskParams().nodeName,
        taskParams().universeUUID);
    String errorString = null;

    try {
      checkUniverseVersion();
      // Update the DB to prevent other changes from happening.
      Universe universe = lockUniverseForUpdate(taskParams().expectedUniverseVersion);

      final NodeDetails currentNode = universe.getNode(taskParams().nodeName);
      if (currentNode == null) {
        String msg =
            String.format("No node %s in universe %s", taskParams().nodeName, universe.name);
        log.error(msg);
        throw new RuntimeException(msg);
      }

      currentNode.validateActionOnState(NodeActionType.ADD);

      preTaskActions();

      Cluster cluster = taskParams().getClusterByUuid(currentNode.placementUuid);
      UserIntent userIntent = cluster.userIntent;
      boolean wasDecommissioned = currentNode.state == NodeState.Decommissioned;

      // For onprem universes, allocate an available node
      // from the provider's node_instance table.
      if (wasDecommissioned && userIntent.providerType.equals(CloudType.onprem)) {
        Optional<NodeInstance> nodeInstance = NodeInstance.maybeGetByName(currentNode.nodeName);
        if (nodeInstance.isPresent()) {
          // Illegal state if it is unused because both node name and in-use fields are updated
          // together.
          checkState(nodeInstance.get().isInUse(), "Node name is set but the node is not in use");
        } else {
          // Reserve a node if it is not assigned yet, and persist the universe details and node
          // reservation in transaction so that universe is aware of the reservation.
          Map<UUID, List<String>> onpremAzToNodes =
              Collections.singletonMap(
                  currentNode.azUuid, Collections.singletonList(currentNode.nodeName));
          universe =
              saveUniverseDetails(
                  u -> {
                    NodeDetails node = u.getNode(taskParams().nodeName);
                    Map<String, NodeInstance> nodeMap =
                        NodeInstance.pickNodes(
                            onpremAzToNodes, currentNode.cloudInfo.instance_type);
                    node.nodeUuid = nodeMap.get(currentNode.nodeName).getNodeUuid();
                    currentNode.nodeUuid = node.nodeUuid;
                    // This needs to be set because DB fetch of this universe later can override the
                    // field as the universe details object is transient and not tracked by DB.
                    u.setUniverseDetails(u.getUniverseDetails());
                    // Perform preflight check. If it fails, the node must not be in use,
                    // otherwise running it second time can succeed. This check must be
                    // performed only when a new node is picked as Add after Remove can
                    // leave processes that require sudo access.
                    String preflightStatus =
                        performPreflightCheck(
                            cluster,
                            currentNode,
                            EncryptionInTransitUtil.isRootCARequired(taskParams())
                                ? taskParams().rootCA
                                : null,
                            EncryptionInTransitUtil.isClientRootCARequired(taskParams())
                                ? taskParams().clientRootCA
                                : null);
                    if (preflightStatus != null) {
                      throw new RuntimeException(
                          String.format(
                              "Node %s (%s) failed preflight check. Error: %s",
                              node.getNodeName(), node.getNodeUuid(), preflightStatus));
                    }
                  });
        }
      }

      Set<NodeDetails> nodeSet = Collections.singleton(currentNode);
      // Update Node State to being added.
      createSetNodeStateTask(currentNode, NodeState.Adding)
          .setSubTaskGroupType(SubTaskGroupType.StartingNode);

      // First spawn an instance for Decommissioned node.
      if (wasDecommissioned) {
        createCreateServerTasks(nodeSet).setSubTaskGroupType(SubTaskGroupType.Provisioning);

        createServerInfoTasks(nodeSet).setSubTaskGroupType(SubTaskGroupType.Provisioning);

        createSetupServerTasks(nodeSet).setSubTaskGroupType(SubTaskGroupType.Provisioning);
      }

      // Re-install software.
      // TODO: Remove the need for version for existing instance, NodeManger needs changes.
      createConfigureServerTasks(nodeSet, params -> params.isMasterInShellMode = true)
          .setSubTaskGroupType(SubTaskGroupType.InstallingSoftware);

      // All necessary nodes are created. Data moving will coming soon.
      createSetNodeStateTasks(nodeSet, NodeDetails.NodeState.ToJoinCluster)
          .setSubTaskGroupType(SubTaskGroupType.Provisioning);

      // Bring up any masters, as needed.
      boolean masterAdded = false;
      if (areMastersUnderReplicated(currentNode, universe)) {
        log.info(
            "Bringing up master for under replicated universe {} ({})",
            universe.universeUUID,
            universe.name);

        // Set gflags for master.
        createGFlagsOverrideTasks(
            nodeSet,
            ServerType.MASTER,
            true /* isShell */,
            VmUpgradeTaskType.None,
            false /*ignoreUseCustomImageConfig*/);

        // Start a shell master process.
        createStartMasterTasks(nodeSet).setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);

        // Mark node as a master in YW DB.
        // Do this last so that master addresses does not pick up current node.
        createUpdateNodeProcessTask(taskParams().nodeName, ServerType.MASTER, true)
            .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);

        // Wait for master to be responsive.
        createWaitForServersTasks(nodeSet, ServerType.MASTER)
            .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

        // Add it into the master quorum.
        createChangeConfigTask(currentNode, true, SubTaskGroupType.WaitForDataMigration);

        masterAdded = true;
      }

      // Set gflags for the tserver.
      createGFlagsOverrideTasks(nodeSet, ServerType.TSERVER);

      // Add the tserver process start task.
      createTServerTaskForNode(currentNode, "start")
          .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);

      // Mark the node as tserver in the YW DB.
      createUpdateNodeProcessTask(taskParams().nodeName, ServerType.TSERVER, true)
          .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);

      // Wait for new tablet servers to be responsive.
      createWaitForServersTasks(nodeSet, ServerType.TSERVER)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Update the swamper target file.
      createSwamperTargetUpdateTask(false /* removeFile */);

      // Clear the host from master's blacklist.
      if (currentNode.state == NodeState.Removed) {
        createModifyBlackListTask(nodeSet, false /* isAdd */, false /* isLeaderBlacklist */)
            .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      }

      // Wait for the master leader to hear from all tservers.
      createWaitForTServerHeartBeatsTask().setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Wait for load to balance.
      createWaitForLoadBalanceTask().setSubTaskGroupType(SubTaskGroupType.WaitForDataMigration);

      if (masterAdded) {
        // Update all tserver conf files with new master information.
        createMasterInfoUpdateTask(universe, currentNode);

        // Update the master addresses on the target universes whose source universe belongs to
        // this task.
        createXClusterConfigUpdateMasterAddressesTask();
      }

      // Update node state to live.
      createSetNodeStateTask(currentNode, NodeState.Live)
          .setSubTaskGroupType(SubTaskGroupType.StartingNode);

      // Update the DNS entry for this universe.
      createDnsManipulationTask(DnsManager.DnsCommandType.Edit, false, userIntent)
          .setSubTaskGroupType(SubTaskGroupType.StartingNode);

      // Mark universe task state to success.
      createMarkUniverseUpdateSuccessTasks().setSubTaskGroupType(SubTaskGroupType.StartingNode);
      // Run all the tasks.
      getRunnableTask().runSubTasks();
    } catch (Throwable t) {
      log.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);
      errorString = t.getMessage();
      throw t;
    } finally {
      // Mark the update of the universe as done. This will allow future updates to the universe.
      unlockUniverseForUpdate(errorString);
    }
    log.info("Finished {} task.", getName());
  }
}
