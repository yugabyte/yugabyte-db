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

import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.DnsManager;
import com.yugabyte.yw.common.NodeActionType;
import com.yugabyte.yw.common.certmgmt.EncryptionInTransitUtil;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
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
@Retryable
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
        taskParams().getUniverseUUID());
    String errorString = null;

    try {
      checkUniverseVersion();
      // Update the DB to prevent other changes from happening.
      Universe universe = lockUniverseForUpdate(taskParams().expectedUniverseVersion);

      final NodeDetails currentNode = universe.getNode(taskParams().nodeName);
      if (currentNode == null) {
        String msg =
            String.format("No node %s in universe %s", taskParams().nodeName, universe.getName());
        log.error(msg);
        throw new RuntimeException(msg);
      }

      // Validate state check for Action only on first try as the task could fail on various
      // intermediate states
      if (isFirstTry()) {
        currentNode.validateActionOnState(NodeActionType.ADD);
      } else {
        log.info(
            "Retrying task to add node {} in state {}", taskParams().nodeName, currentNode.state);
      }

      preTaskActions();

      Cluster cluster = universe.getUniverseDetails().getClusterByUuid(currentNode.placementUuid);

      UserIntent userIntent = cluster.userIntent;
      boolean wasDecommissioned = currentNode.state == NodeState.Decommissioned;

      // For on-prem universes, allocate an available node
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
                    performPreflightCheck(
                        cluster,
                        currentNode,
                        EncryptionInTransitUtil.isRootCARequired(taskParams())
                            ? taskParams().rootCA
                            : null,
                        EncryptionInTransitUtil.isClientRootCARequired(taskParams())
                            ? taskParams().getClientRootCA()
                            : null);
                  });
        }
      }

      Set<NodeDetails> nodeSet = Collections.singleton(currentNode);

      if (!wasDecommissioned) {
        // Validate instance existence and connectivity before changing the state.
        createInstanceExistsCheckTasks(universe.getUniverseUUID(), nodeSet);
      }

      // Update Node State to being added if it is not in one of the intermediate states.
      // We must be successful in setting node state to Adding on initial state, even on retry.
      if (currentNode.state == NodeState.Removed || currentNode.state == NodeState.Decommissioned) {
        createSetNodeStateTask(currentNode, NodeState.Adding)
            .setSubTaskGroupType(SubTaskGroupType.StartingNode);
      }

      // First spawn an instance for Decommissioned node.
      // ignore node status is true because generic callee checks for node state To Be Added.
      boolean isNextFallThrough =
          createCreateNodeTasks(
              universe, nodeSet, true /* ignoreNodeStatus */, null /* param customizer */);

      boolean addMaster =
          areMastersUnderReplicated(currentNode, universe)
              && (currentNode.dedicatedTo == null || currentNode.dedicatedTo == ServerType.MASTER);
      boolean addTServer =
          currentNode.dedicatedTo == null || currentNode.dedicatedTo == ServerType.TSERVER;

      Set<NodeDetails> mastersToAdd = null;
      Set<NodeDetails> tServersToAdd = null;
      if (addMaster) {
        mastersToAdd = nodeSet;
      }
      if (addTServer) {
        tServersToAdd = nodeSet;
      }
      // State checking is disabled as generic callee checks for node to be in ServerSetup state.
      // 1. Install software
      // 2. Set GFlags for master and TServer as applicable.
      // 3. All necessary node setup done and node is ready to join cluster.
      createConfigureNodeTasks(
          universe,
          mastersToAdd,
          tServersToAdd,
          isNextFallThrough /* ignoreNodeStatus */,
          installSoftwareParams -> installSoftwareParams.isMasterInShellMode = true,
          gFlagsParams -> {
            gFlagsParams.isMasterInShellMode = true;
            gFlagsParams.resetMasterState = true;
          });

      // Copy the source root certificate to the newly added node.
      createTransferXClusterCertsCopyTasks(
          Collections.singleton(currentNode), universe, SubTaskGroupType.Provisioning);

      // Bring up any masters, as needed.
      if (addMaster) {
        log.info(
            "Bringing up master for under replicated universe {} ({})",
            universe.getUniverseUUID(),
            universe.getName());

        // Start a shell master process.
        createStartMasterTasks(nodeSet).setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);

        // Add it into the master quorum.
        createChangeConfigTasks(currentNode, true, SubTaskGroupType.StartingNodeProcesses);

        // Wait for master to be responsive.
        createWaitForServersTasks(nodeSet, ServerType.MASTER)
            .setSubTaskGroupType(SubTaskGroupType.StartingMasterProcess);

        // Mark node as a master in YW DB.
        // Do this last so that master addresses does not pick up current node.
        createUpdateNodeProcessTask(taskParams().nodeName, ServerType.MASTER, true)
            .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);
      }

      // Bring up TServers, as needed.
      if (addTServer) {
        // Add the tserver process start task.
        createTServerTaskForNode(currentNode, "start")
            .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);

        // Mark the node as tserver in the YW DB.
        createUpdateNodeProcessTask(taskParams().nodeName, ServerType.TSERVER, true)
            .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);

        // Wait for new tablet servers to be responsive.
        createWaitForServersTasks(nodeSet, ServerType.TSERVER)
            .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);

        // [PLAT-5637] Wait for postgres server to be healthy if YSQL is enabled.
        if (userIntent.enableYSQL) {
          createWaitForServersTasks(nodeSet, ServerType.YSQLSERVER)
              .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);
        }
      }

      if (universe.isYbcEnabled()) {
        createStartYbcTasks(nodeSet).setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);

        // Wait for yb-controller to be responsive on current node.
        createWaitForYbcServerTask(nodeSet)
            .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);
      }

      // Update the swamper target file.
      createSwamperTargetUpdateTask(false /* removeFile */);

      // Clear the host from master's blacklist.
      createModifyBlackListTask(nodeSet, false /* isAdd */, false /* isLeaderBlacklist */)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Wait for the master leader to hear from all tservers.
      createWaitForTServerHeartBeatsTask().setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      if (confGetter.getConfForScope(universe, UniverseConfKeys.waitForLbForAddedNodes)) {
        // Wait for load to balance.
        createWaitForLoadBalanceTask().setSubTaskGroupType(SubTaskGroupType.WaitForDataMigration);
      }

      // For idempotency, ensure we run this block again as before the failure master would have
      // been added to DB and this block might not have been run.
      if (addMaster || (currentNode.isMaster && !isFirstTry())) {
        // Update master addresses including xcluster with new master information.
        createMasterInfoUpdateTask(universe, currentNode, null);
      }

      // Add node to load balancer.
      createManageLoadBalancerTasks(
          createLoadBalancerMap(
              universe.getUniverseDetails(),
              ImmutableList.of(cluster),
              null,
              ImmutableSet.of(currentNode)));

      // Update the DNS entry for this universe.
      createDnsManipulationTask(DnsManager.DnsCommandType.Edit, false, universe)
          .setSubTaskGroupType(SubTaskGroupType.StartingNode);

      // Update node state to live.
      createSetNodeStateTask(currentNode, NodeState.Live)
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
