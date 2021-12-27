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

import static com.yugabyte.yw.forms.UniverseTaskParams.isFirstTryForTask;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.DnsManager;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.PlacementInfoUtil.SelectMastersResult;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.MasterState;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

// Tracks edit intents to the cluster and then performs the sequence of configuration changes on
// this universe to go from the current set of master/tserver nodes to the final configuration.
@Slf4j
public class EditUniverse extends UniverseDefinitionTaskBase {

  @Inject
  protected EditUniverse(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  public void run() {
    log.info("Started {} task for uuid={}", getName(), taskParams().universeUUID);
    String errorString = null;

    try {
      checkUniverseVersion();
      if (isFirstTryForTask(taskParams())) {
        // Verify the task params.
        verifyParams(UniverseOpType.EDIT);
      }
      // Create the task list sequence.
      subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);

      // Update the universe DB with the changes to be performed and set the 'updateInProgress' flag
      // to prevent other updates from happening.
      Universe universe =
          lockUniverseForUpdate(
              taskParams().expectedUniverseVersion,
              u -> {
                if (isFirstTryForTask(taskParams())) {
                  // TODO Transaction is required mainly because validations are done here.
                  // Set all the node names.
                  setNodeNames(u);
                  // Set non on-prem node UUIDs.
                  setCloudNodeUuids(u);
                  // Update on-prem node UUIDs.
                  updateOnPremNodeUuidsOnTaskParams();
                  // Perform pre-task actions.
                  preTaskActions();
                  // Run preflight checks on onprem nodes to be added.
                  if (!performUniversePreflightChecks(taskParams().clusters)) {
                    throw new RuntimeException("Preflight checks failed.");
                  }
                  // Select master nodes, if needed. Changes in masters are not automatically
                  // applied.
                  SelectMastersResult selection = selectMasters(u.getMasterLeaderHostText());
                  verifyMastersSelection(selection);

                  // Applying changes to master flags for added masters only.
                  // We are not clearing this flag according to selection.removedMasters in case
                  // the master leader is to be changed and until the master leader is switched to
                  // the new one.
                  selection.addedMasters.forEach(
                      n -> {
                        n.isMaster = true;
                        n.masterState = MasterState.ToStart;
                      });
                  selection.removedMasters.forEach(
                      n -> {
                        n.masterState = MasterState.ToStop;
                      });
                  // Set the prepared data to universe in-memory.
                  setUserIntentToUniverse(u, taskParams(), false);
                  // Task params contain the exact blueprint of what is desired.
                  // There is a rare possibility that this succeeds and
                  // saving the Universe fails. It is ok because the retry
                  // will just fail.
                  updateTaskDetailsInDB(taskParams());
                }
              });

      Set<NodeDetails> addedMasters =
          taskParams()
              .nodeDetailsSet
              .stream()
              .filter(n -> n.masterState == MasterState.ToStart)
              .collect(Collectors.toSet());
      Set<NodeDetails> removedMasters =
          taskParams()
              .nodeDetailsSet
              .stream()
              .filter(n -> n.masterState == MasterState.ToStop)
              .collect(Collectors.toSet());
      boolean updateMasters = !addedMasters.isEmpty() || !removedMasters.isEmpty();
      for (Cluster cluster : taskParams().clusters) {
        editCluster(
            universe,
            cluster,
            getNodesInCluster(cluster.uuid, addedMasters),
            getNodesInCluster(cluster.uuid, removedMasters),
            updateMasters);
      }

      // Wait for the master leader to hear from all tservers.
      createWaitForTServerHeartBeatsTask().setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Update the DNS entry for this universe, based in primary provider info.
      UserIntent primaryIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
      createDnsManipulationTask(DnsManager.DnsCommandType.Edit, false, primaryIntent)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Marks the update of this universe as a success only if all the tasks before it succeeded.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      // Run all the tasks.
      subTaskGroupQueue.run();
    } catch (Throwable t) {
      log.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);
      errorString = t.getMessage();
      throw t;
    } finally {
      // Mark the update of the universe as done. This will allow future edits/updates to the
      // universe to happen.
      unlockUniverseForUpdate(errorString);
    }
    log.info("Finished {} task.", getName());
  }

  private void editCluster(
      Universe universe,
      Cluster cluster,
      Set<NodeDetails> newMasters,
      Set<NodeDetails> mastersToStop,
      boolean updateMasters) {
    UserIntent userIntent = cluster.userIntent;
    Set<NodeDetails> nodes = taskParams().getNodesInCluster(cluster.uuid);

    log.info(
        "Configure numNodes={}, Replication factor={}",
        userIntent.numNodes,
        userIntent.replicationFactor);

    Set<NodeDetails> nodesToBeRemoved = PlacementInfoUtil.getNodesToBeRemoved(nodes);

    Set<NodeDetails> nodesToProvision = PlacementInfoUtil.getNodesToProvision(nodes);

    Set<NodeDetails> existingNodesToStartMaster =
        newMasters.stream().filter(n -> n.state != NodeState.ToBeAdded).collect(Collectors.toSet());

    // Set the old nodes' state to to-be-removed.
    if (!nodesToBeRemoved.isEmpty()) {
      createSetNodeStateTasks(nodesToBeRemoved, NodeDetails.NodeState.ToBeRemoved)
          .setSubTaskGroupType(SubTaskGroupType.Provisioning);
    }

    Set<NodeDetails> liveNodes = PlacementInfoUtil.getLiveNodes(nodes);

    // Update any tags on nodes that are not going to be removed and not being added.
    Cluster existingCluster = universe.getCluster(cluster.uuid);
    if (!cluster.areTagsSame(existingCluster)) {
      log.info(
          "Tags changed from '{}' to '{}'.",
          existingCluster.userIntent.instanceTags,
          cluster.userIntent.instanceTags);
      createUpdateInstanceTagsTasks(
          getNodesInCluster(cluster.uuid, liveNodes),
          Util.getKeysNotPresent(
              existingCluster.userIntent.instanceTags, cluster.userIntent.instanceTags));
    }

    if (!nodesToProvision.isEmpty()) {
      Map<UUID, List<NodeDetails>> nodesPerAZ =
          nodes
              .stream()
              .filter(
                  n ->
                      n.state != NodeDetails.NodeState.ToBeAdded
                          && n.state != NodeDetails.NodeState.ToBeRemoved)
              .collect(Collectors.groupingBy(n -> n.azUuid));

      nodesToProvision.forEach(
          node -> {
            Set<String> machineImages =
                nodesPerAZ
                    .getOrDefault(node.azUuid, Collections.emptyList())
                    .stream()
                    .map(n -> n.machineImage)
                    .collect(Collectors.toSet());
            Iterator<String> iterator = machineImages.iterator();

            if (iterator.hasNext()) {
              String imageToUse = iterator.next();

              if (iterator.hasNext()) {
                log.warn(
                    "Nodes in AZ {} are based on different machine images: {},"
                        + " falling back to default",
                    node.cloudInfo.az,
                    String.join(", ", machineImages));
              } else {
                node.machineImage = imageToUse;
              }
            }
          });

      // Provision the nodes.
      // State checking is enabled because the subtasks are not idempotent.
      createProvisionNodeTasks(
          universe, nodesToProvision, true /* isShell */, false /* ignore node status check */);
    }

    Set<NodeDetails> removeMasters = PlacementInfoUtil.getMastersToBeRemoved(nodes);
    Set<NodeDetails> tserversToBeRemoved = PlacementInfoUtil.getTserversToBeRemoved(nodes);

    // Ensure all masters are covered in nodes to be removed.
    if (!removeMasters.isEmpty()) {
      if (nodesToBeRemoved.isEmpty()) {
        String errMsg = "If masters are being removed, corresponding nodes need removal too.";
        log.error(errMsg + " masters: " + nodeNames(removeMasters));
        throw new IllegalStateException(errMsg);
      }
      if (!nodesToBeRemoved.containsAll(removeMasters)) {
        String errMsg = "If masters are being removed, all those nodes need removal too.";
        log.error(
            errMsg
                + " masters: "
                + nodeNames(removeMasters)
                + " , but removing only "
                + nodeNames(nodesToBeRemoved));
        throw new IllegalStateException(errMsg);
      }
    }
    removeMasters.addAll(mastersToStop);

    // Creates the primary cluster by first starting the masters.
    if (!newMasters.isEmpty()) {
      if (cluster.clusterType == ClusterType.ASYNC) {
        String errMsg = "Read-only cluster " + cluster.uuid + " should not have masters.";
        log.error(errMsg);
        throw new IllegalStateException(errMsg);
      }
      if (!existingNodesToStartMaster.isEmpty()) {
        // This changes the state of Master to Configured.
        // State check is done for these tasks because this is modified after the
        // master is started. It will reset the later change otherwise.
        createConfigureMasterTasks(
            universe,
            existingNodesToStartMaster,
            true /* shell mode */,
            false /* ignore node status check */);
      }
      // Start masters. If it is already started, it has no effect.
      createStartMasterProcessTasks(newMasters);
    }

    Set<NodeDetails> newTservers = PlacementInfoUtil.getTserversToProvision(nodes);
    if (!newTservers.isEmpty()) {
      // Blacklist all the new tservers before starting so that they do not join.
      // Idempotent as same set of servers are blacklisted.
      createModifyBlackListTask(newTservers, null /* To remove */, false /* isLeaderBlacklist */)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Start tservers on all nodes.
      createStartTserverProcessTasks(newTservers);
    }
    if (!nodesToProvision.isEmpty()) {
      // Set the new nodes' state to live.
      createSetNodeStateTasks(nodesToProvision, NodeDetails.NodeState.Live)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
    }

    // Swap the blacklisted tservers.
    // Idempotent as same set of servers are either blacklisted or removed.
    createModifyBlackListTask(tserversToBeRemoved, newTservers, false /* isLeaderBlacklist */)
        .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

    // Update placement info on master leader.
    createPlacementInfoTask(null /* additional blacklist */)
        .setSubTaskGroupType(SubTaskGroupType.WaitForDataMigration);

    // Update the swamper target file.
    createSwamperTargetUpdateTask(false /* removeFile */);

    if (!nodesToBeRemoved.isEmpty()) {
      // Wait for %age completion of the tablet move from master.
      createWaitForDataMoveTask().setSubTaskGroupType(SubTaskGroupType.WaitForDataMigration);
    } else {
      if (!tserversToBeRemoved.isEmpty()) {
        String errMsg = "Universe shrink should have been handled using node decommision.";
        log.error(errMsg);
        throw new IllegalStateException(errMsg);
      }
      // If only tservers are added, wait for load to balance across all tservers.
      createWaitForLoadBalanceTask().setSubTaskGroupType(SubTaskGroupType.WaitForDataMigration);
    }

    if (cluster.clusterType == ClusterType.PRIMARY
        && PlacementInfoUtil.didAffinitizedLeadersChange(
            universe.getUniverseDetails().getPrimaryCluster().placementInfo,
            cluster.placementInfo)) {
      createWaitForLeadersOnPreferredOnlyTask();
    }

    if (!newMasters.isEmpty()) {
      // Now finalize the master quorum change tasks.
      createMoveMastersTasks(SubTaskGroupType.WaitForDataMigration, newMasters, removeMasters);

      if (!mastersToStop.isEmpty()) {
        createStopMasterTasks(mastersToStop)
            .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      }

      // Wait for a master leader to be elected.
      createWaitForMasterLeaderTask().setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Update these older ones to be not masters anymore so tserver info can be updated with the
      // final master list and other future cluster client operations.
      createUpdateNodeProcessTasks(removeMasters, ServerType.MASTER, false);
    }

    if (updateMasters) {
      // Change the master addresses in the conf file for all tservers.
      Set<NodeDetails> allTservers = new HashSet<>(newTservers);
      allTservers.addAll(liveNodes);

      createConfigureServerTasks(allTservers, false /* isShell */, true /* updateMasterAddrs */);
      createSetFlagInMemoryTasks(
              allTservers,
              ServerType.TSERVER,
              true /* force flag update */,
              null /* no gflag to update */,
              true /* updateMasterAddrs */)
          .setSubTaskGroupType(SubTaskGroupType.UpdatingGFlags);

      Set<NodeDetails> allMasters = new HashSet<>(newMasters);
      Set<String> takenMasters =
          allMasters.stream().map(n -> n.nodeName).collect(Collectors.toSet());
      allMasters.addAll(
          liveNodes
              .stream()
              .filter(
                  n ->
                      n.isMaster
                          && !takenMasters.contains(n.nodeName)
                          && !mastersToStop.contains(n))
              .collect(Collectors.toSet()));

      // Change the master addresses in the conf file for the new masters.
      // Update the same set of master addresses.
      createConfigureServerTasks(
          allMasters, false /* isShell */, true /* updateMasterAddrs */, true /* isMaster */);
      createSetFlagInMemoryTasks(
              allMasters,
              ServerType.MASTER,
              true /* force flag update */,
              null /* no gflag to update */,
              true /* updateMasterAddrs */)
          .setSubTaskGroupType(SubTaskGroupType.UpdatingGFlags);
    }

    // Finally send destroy to the old set of nodes and remove them from this universe.
    if (!nodesToBeRemoved.isEmpty()) {
      createDestroyServerTasks(nodesToBeRemoved, false /* isForceDelete */, true /* deleteNode */)
          .setSubTaskGroupType(SubTaskGroupType.RemovingUnusedServers);
    }

    // Clear blacklisted tservers.
    createModifyBlackListTask(null, tserversToBeRemoved, false /* isLeaderBlacklist */)
        .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
  }

  /**
   * Fills in the series of steps needed to move the masters using the node names. The actual node
   * details (such as ip addresses) are found at runtime by querying the database.
   */
  private void createMoveMastersTasks(
      SubTaskGroupType subTask, Set<NodeDetails> newMasters, Set<NodeDetails> removeMasters) {

    // Get the list of node names to add as masters.
    List<NodeDetails> mastersToAdd = new ArrayList<>(newMasters);
    // Get the list of node names to remove as masters.
    List<NodeDetails> mastersToRemove = new ArrayList<>(removeMasters);

    // Find the minimum number of master changes where we can perform an add followed by a remove.
    int numIters = Math.min(mastersToAdd.size(), mastersToRemove.size());

    // Perform a master add followed by a remove if possible. Need not remove the (current) master
    // leader last - even if we get current leader, it might change by the time we run the actual
    // task. So we might do multiple leader stepdown's, which happens automatically in the
    // client code during the task's run.
    for (int idx = 0; idx < numIters; idx++) {
      createChangeConfigTask(mastersToAdd.get(idx), true, subTask);
      // Do not use useHostPort = true because retry is not done for the option
      // when the leader itself is being removed. The retryable error code
      // LEADER_NEEDS_STEP_DOWN is reported only when useHostPort = false.
      createChangeConfigTask(mastersToRemove.get(idx), false, subTask, false);
    }

    // Perform any additions still left.
    for (int idx = numIters; idx < newMasters.size(); idx++) {
      createChangeConfigTask(mastersToAdd.get(idx), true, subTask);
    }

    // Perform any removals still left.
    for (int idx = numIters; idx < removeMasters.size(); idx++) {
      createChangeConfigTask(mastersToRemove.get(idx), false, subTask, false);
    }
  }
}
