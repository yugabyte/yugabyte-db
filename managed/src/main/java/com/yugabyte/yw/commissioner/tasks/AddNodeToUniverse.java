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
import java.util.Collection;
import java.util.Collections;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import javax.annotation.Nullable;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

// Allows the addition of a node into a universe. Spawns the necessary processes - tserver
// and/or master and ensures the task waits for the right set of load balance primitives.
@Slf4j
@Retryable
public class AddNodeToUniverse extends UniverseDefinitionTaskBase {

  private boolean addMaster;
  private boolean addTserver;
  private NodeDetails currentNode;

  @Inject
  protected AddNodeToUniverse(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected NodeTaskParams taskParams() {
    return (NodeTaskParams) taskParams;
  }

  private void runBasicChecks(Universe universe) {
    NodeDetails currentNode = universe.getNode(taskParams().nodeName);
    if (currentNode == null) {
      String msg =
          String.format(
              "No node %s is found in universe %s", taskParams().nodeName, universe.getName());
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
    Cluster cluster = universe.getUniverseDetails().getClusterByUuid(currentNode.placementUuid);
    UserIntent userIntent = cluster.userIntent;

    if (currentNode.state == NodeState.Decommissioned
        && userIntent.providerType.equals(CloudType.onprem)) {
      Optional<NodeInstance> nodeInstance = NodeInstance.maybeGetByName(currentNode.nodeName);
      if (nodeInstance.isPresent()) {
        // Illegal state if it is unused because both node name and in-use fields are updated
        // together.
        checkState(
            nodeInstance.get().getState().equals(NodeInstance.State.USED),
            "Node name is set but the node is not in use");
      }
    }
  }

  @Override
  public void validateParams(boolean isFirstTry) {
    super.validateParams(isFirstTry);
    runBasicChecks(getUniverse());
  }

  private void configureNodeDetails(Universe universe) {
    Cluster cluster = universe.getUniverseDetails().getClusterByUuid(currentNode.placementUuid);
    UserIntent userIntent = cluster.userIntent;
    Optional<NodeInstance> nodeInstance = NodeInstance.maybeGetByName(currentNode.nodeName);

    if (currentNode.state == NodeState.Decommissioned
        && userIntent.providerType.equals(CloudType.onprem)
        && !nodeInstance.isPresent()) {
      reserveOnPremNodes(cluster, Collections.singleton(currentNode), false /* commit changes */);
      // Clone the node because isMaster and isTserver are not set yet in currentNode.
      NodeDetails currentNodeClone = currentNode.clone();
      currentNodeClone.isMaster = addMaster;
      currentNodeClone.isTserver = addTserver;
      createPreflightNodeCheckTasks(
          Collections.singleton(cluster),
          Collections.singleton(currentNodeClone),
          EncryptionInTransitUtil.isRootCARequired(taskParams()) ? taskParams().rootCA : null,
          EncryptionInTransitUtil.isClientRootCARequired(taskParams())
              ? taskParams().getClientRootCA()
              : null);

      createCheckCertificateConfigTask(
          Collections.singleton(cluster),
          Collections.singleton(currentNodeClone),
          taskParams().rootCA,
          EncryptionInTransitUtil.isClientRootCARequired(taskParams())
              ? taskParams().getClientRootCA()
              : null,
          userIntent.enableClientToNodeEncrypt);
    }
  }

  private void freezeUniverseInTxn(Universe universe) {
    NodeDetails universeNode = universe.getNode(taskParams().nodeName);
    universeNode.nodeUuid = currentNode.nodeUuid;
    // Confirm the node on hold.
    commitReservedNodes();
  }

  @Override
  protected void createPrecheckTasks(Universe universe) {
    // Check again after locking.
    runBasicChecks(universe);
    currentNode = universe.getNode(taskParams().nodeName);
    addMaster =
        areMastersUnderReplicated(currentNode, universe)
            && (currentNode.dedicatedTo == null || currentNode.dedicatedTo == ServerType.MASTER);
    addTserver = currentNode.dedicatedTo == null || currentNode.dedicatedTo == ServerType.TSERVER;
    if (currentNode.state != NodeState.Decommissioned) {
      // Validate instance existence and connectivity before changing the state.
      createInstanceExistsCheckTasks(
          universe.getUniverseUUID(), taskParams(), Collections.singletonList(currentNode));
    }
    addBasicPrecheckTasks();
    if (isFirstTry()) {
      configureNodeDetails(universe);
    }
  }

  @Override
  public void run() {
    if (maybeRunOnlyPrechecks()) {
      return;
    }
    log.info(
        "Started {} task for node {} in universe {}",
        getName(),
        taskParams().nodeName,
        taskParams().getUniverseUUID());
    Universe universe = null;
    try {
      universe =
          lockAndFreezeUniverseForUpdate(
              taskParams().expectedUniverseVersion, this::freezeUniverseInTxn);
      final NodeDetails currentNode = universe.getNode(taskParams().nodeName);

      preTaskActions();

      Cluster cluster = universe.getUniverseDetails().getClusterByUuid(currentNode.placementUuid);

      UserIntent userIntent = cluster.userIntent;

      // For on-prem universes, allocate an available node
      // from the provider's node_instance table.

      Set<NodeDetails> nodeSet = Collections.singleton(currentNode);

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

      Set<NodeDetails> mastersToAdd = null;
      Set<NodeDetails> tServersToAdd = null;
      if (addMaster) {
        mastersToAdd = nodeSet;
      }
      if (addTserver) {
        tServersToAdd = nodeSet;
      }
      // State checking is disabled as generic callee checks for node to be in ServerSetup
      // state.
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

      // Make sure clock skew is low enough.
      createWaitForClockSyncTasks(universe, nodeSet)
          .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);

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

      // Bring up Tservers, as needed.
      if (addTserver) {
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
        // Set this in memory too.
        currentNode.isTserver = true;
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
      createModifyBlackListTask(
              null /* addNodes */, nodeSet /*removeNodes */, false /* isLeaderBlacklist */)
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
        createMasterInfoUpdateTask(universe, currentNode, null /* stopped node */);
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
      throw t;
    } finally {
      releaseReservedNodes();
      if (universe != null) {
        unlockUniverseForUpdate(universe.getUniverseUUID());
      }
    }
    log.info("Finished {} task.", getName());
  }

  public void createCheckCertificateConfigTask(
      Collection<Cluster> clusters,
      Set<NodeDetails> nodes,
      @Nullable UUID rootCA,
      @Nullable UUID clientRootCA,
      boolean enableClientToNodeEncrypt) {
    createCheckCertificateConfigTask(
        clusters, nodes, rootCA, clientRootCA, enableClientToNodeEncrypt, null);
  }
}
