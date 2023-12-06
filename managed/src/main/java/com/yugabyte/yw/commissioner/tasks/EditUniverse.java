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

import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.DnsManager;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.PlacementInfoUtil.SelectMastersResult;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.MasterState;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.*;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

// Tracks edit intents to the cluster and then performs the sequence of configuration changes on
// this universe to go from the current set of master/tserver nodes to the final configuration.
@Slf4j
@Abortable
@Retryable
public class EditUniverse extends UniverseDefinitionTaskBase {

  @Inject
  protected EditUniverse(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  public void validateParams(boolean isFirstTry) {
    super.validateParams(isFirstTry);
    if (isFirstTry) {
      // Verify the task params.
      verifyParams(UniverseOpType.EDIT);
    }
  }

  protected void freezeUniverseInTxn(Universe universe) {
    // Fetch the task params from the DB to start from fresh on retry.
    // Otherwise, some operations like name assignment can fail.
    fetchTaskDetailsFromDB();
    // TODO Transaction is required mainly because validations are done here.
    // Set all the node names.
    setNodeNames(universe);
    // Set non on-prem node UUIDs.
    setCloudNodeUuids(universe);
    // Update on-prem node UUIDs.
    updateOnPremNodeUuidsOnTaskParams();
    // Perform pre-task actions.
    preTaskActions(universe);
    // Select master nodes, if needed. Changes in masters are not automatically
    // applied.
    SelectMastersResult selection = selectMasters(universe.getMasterLeaderHostText());
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
    // UserIntent in universe will be pointing to userIntent from params after
    // setUserIntentToUniverse. So we need to store tags locally to be able to reset
    // them later.
    Map<UUID, Map<String, String>> currentTags =
        universe.getUniverseDetails().clusters.stream()
            .collect(Collectors.toMap(c -> c.uuid, c -> c.userIntent.instanceTags));
    // Set the prepared data to universe in-memory.
    setUserIntentToUniverse(universe, taskParams(), false);
    // Task params contain the exact blueprint of what is desired.
    // There is a rare possibility that this succeeds and
    // saving the Universe fails. It is ok because the retry
    // will just fail.
    updateTaskDetailsInDB(taskParams());
    // We need to reset tags, to make this tags update retryable.
    // New tags will be written into DB in UpdateUniverseTags task.
    for (Cluster cluster : universe.getUniverseDetails().clusters) {
      cluster.userIntent.instanceTags = currentTags.get(cluster.uuid);
    }
  }

  @Override
  public void run() {
    log.info("Started {} task for uuid={}", getName(), taskParams().getUniverseUUID());
    checkUniverseVersion();
    String errorString = null;
    Map<UUID, Map<String, String>> tagsToUpdate = new HashMap<>();
    Universe universe = getUniverse();
    // The universe parameter in this callback has local changes which may be needed by
    // the methods inside e.g updateInProgress field.
    for (Cluster cluster : taskParams().clusters) {
      Cluster originalCluster = universe.getCluster(cluster.uuid);
      if (!originalCluster.areTagsSame(cluster)) {
        tagsToUpdate.put(cluster.uuid, cluster.userIntent.instanceTags);
      }
    }
    universe =
        lockAndFreezeUniverseForUpdate(
            taskParams().expectedUniverseVersion, this::freezeUniverseInTxn);
    try {

      // Create preflight node check tasks for on-prem nodes.
      createPreflightNodeCheckTasks(universe, taskParams().clusters);

      Set<NodeDetails> addedMasters =
          taskParams().nodeDetailsSet.stream()
              .filter(n -> n.masterState == MasterState.ToStart)
              .collect(Collectors.toSet());
      Set<NodeDetails> removedMasters =
          taskParams().nodeDetailsSet.stream()
              .filter(n -> n.masterState == MasterState.ToStop)
              .collect(Collectors.toSet());
      boolean updateMasters = !addedMasters.isEmpty() || !removedMasters.isEmpty();
      for (Cluster cluster : taskParams().clusters) {
        editCluster(
            universe,
            cluster,
            getNodesInCluster(cluster.uuid, addedMasters),
            getNodesInCluster(cluster.uuid, removedMasters),
            updateMasters,
            tagsToUpdate);
      }

      // Wait for the master leader to hear from all tservers.
      // NOTE: Universe expansion will fail in the master leader failover scenario - if a node
      // is down externally for >15 minutes and the master leader then marks the node down for
      // real. Then that down TServer will timeout this task and universe expansion will fail.
      createWaitForTServerHeartBeatsTask().setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Update the DNS entry for this universe.
      createDnsManipulationTask(DnsManager.DnsCommandType.Edit, false, universe)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Marks the update of this universe as a success only if all the tasks before it succeeded.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      // Run all the tasks.
      getRunnableTask().runSubTasks();
    } catch (Throwable t) {
      log.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);
      errorString = t.getMessage();
      throw t;
    } finally {
      // Mark the update of the universe as done. This will allow future edits/updates to the
      // universe to happen.
      universe = unlockUniverseForUpdate(errorString);

      if (universe != null
          && universe.getConfig().getOrDefault(Universe.USE_CUSTOM_IMAGE, "false").equals("true")) {
        universe.updateConfig(
            ImmutableMap.of(
                Universe.USE_CUSTOM_IMAGE,
                Boolean.toString(
                    universe.getUniverseDetails().nodeDetailsSet.stream()
                        .allMatch(n -> n.ybPrebuiltAmi))));
        universe.save();
      }
    }
    log.info("Finished {} task.", getName());
  }

  private void editCluster(
      Universe universe,
      Cluster cluster,
      Set<NodeDetails> newMasters,
      Set<NodeDetails> mastersToStop,
      boolean updateMasters,
      Map<UUID, Map<String, String>> tagsToUpdate) {
    UserIntent userIntent = cluster.userIntent;
    Set<NodeDetails> nodes = taskParams().getNodesInCluster(cluster.uuid);

    log.info(
        "Configure numNodes={}, Replication factor={}",
        userIntent.numNodes,
        userIntent.replicationFactor);

    Set<NodeDetails> nodesToBeRemoved = PlacementInfoUtil.getNodesToBeRemoved(nodes);

    Set<NodeDetails> nodesToProvision = PlacementInfoUtil.getNodesToProvision(nodes);

    Set<NodeDetails> removeMasters = PlacementInfoUtil.getMastersToBeRemoved(nodes);

    Set<NodeDetails> tserversToBeRemoved = PlacementInfoUtil.getTserversToBeRemoved(nodes);

    Set<NodeDetails> existingNodesToStartMaster =
        newMasters.stream().filter(n -> n.state != NodeState.ToBeAdded).collect(Collectors.toSet());

    boolean isWaitForLeadersOnPreferred =
        confGetter.getConfForScope(universe, UniverseConfKeys.ybEditWaitForLeadersOnPreferred);

    // Set the old nodes' state to to-be-removed.
    if (!nodesToBeRemoved.isEmpty()) {
      if (nodesToBeRemoved.size() == nodes.size()) {
        // Cluster must be deleted via cluster delete task.
        String errMsg = "All nodes cannot be removed for cluster " + cluster.uuid;
        log.error(errMsg);
        throw new IllegalStateException(errMsg);
      }
      createSetNodeStateTasks(nodesToBeRemoved, NodeDetails.NodeState.ToBeRemoved)
          .setSubTaskGroupType(SubTaskGroupType.Provisioning);
    }

    Set<NodeDetails> liveNodes = PlacementInfoUtil.getLiveNodes(nodes);

    // Update any tags on nodes that are not going to be removed and not being added.
    Cluster existingCluster = universe.getCluster(cluster.uuid);
    if (tagsToUpdate.containsKey(cluster.uuid)) {
      Map<String, String> newTags = tagsToUpdate.get(cluster.uuid);
      log.info("Tags changed from '{}' to '{}'.", existingCluster.userIntent.instanceTags, newTags);
      createUpdateInstanceTagsTasks(
          getNodesInCluster(cluster.uuid, liveNodes),
          newTags,
          Util.getKeysNotPresent(existingCluster.userIntent.instanceTags, newTags));

      createUpdateUniverseTagsTask(cluster, newTags)
          .setSubTaskGroupType(SubTaskGroupType.Provisioning);
    }

    boolean ignoreUseCustomImageConfig =
        !taskParams().nodeDetailsSet.stream().allMatch(n -> n.ybPrebuiltAmi);

    if (!nodesToProvision.isEmpty()) {
      Map<UUID, List<NodeDetails>> nodesPerAZ =
          nodes.stream()
              .filter(
                  n ->
                      n.state != NodeDetails.NodeState.ToBeAdded
                          && n.state != NodeDetails.NodeState.ToBeRemoved)
              .collect(Collectors.groupingBy(n -> n.azUuid));

      nodesToProvision.forEach(
          node -> {
            Set<String> machineImages =
                nodesPerAZ.getOrDefault(node.azUuid, Collections.emptyList()).stream()
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
          universe,
          nodesToProvision,
          false /* ignore node status check */,
          setupServerParams -> {
            setupServerParams.ignoreUseCustomImageConfig = ignoreUseCustomImageConfig;
          },
          installSoftwareParams -> {
            installSoftwareParams.isMasterInShellMode = true;
            installSoftwareParams.ignoreUseCustomImageConfig = ignoreUseCustomImageConfig;
          },
          gFlagsParams -> {
            gFlagsParams.isMasterInShellMode = true;
            gFlagsParams.resetMasterState = true;
            gFlagsParams.ignoreUseCustomImageConfig = ignoreUseCustomImageConfig;
          });
      // Copy the source root certificate to the provisioned nodes.
      createTransferXClusterCertsCopyTasks(
          nodesToProvision, universe, SubTaskGroupType.Provisioning);
    }

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
            false /* ignore node status check */,
            ignoreUseCustomImageConfig);
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
      createStartTserverProcessTasks(newTservers, userIntent.enableYSQL);

      if (universe.isYbcEnabled()) {
        createStartYbcProcessTasks(
            newTservers, universe.getUniverseDetails().getPrimaryCluster().userIntent.useSystemd);
      }
    }
    if (!nodesToProvision.isEmpty()) {
      // Set the new nodes' state to live.
      createSetNodeStateTasks(nodesToProvision, NodeDetails.NodeState.Live)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
    }

    if (!newTservers.isEmpty() || !tserversToBeRemoved.isEmpty()) {
      // Swap the blacklisted tservers.
      // Idempotent as same set of servers are either blacklisted or removed.
      createModifyBlackListTask(tserversToBeRemoved, newTservers, false /* isLeaderBlacklist */)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
    }
    // Update placement info on master leader.
    createPlacementInfoTask(null /* additional blacklist */)
        .setSubTaskGroupType(SubTaskGroupType.WaitForDataMigration);

    if (!newTservers.isEmpty()
        || !newMasters.isEmpty()
        || !tserversToBeRemoved.isEmpty()
        || !removeMasters.isEmpty()
        || !nodesToBeRemoved.isEmpty()) {
      // Update the swamper target file.
      createSwamperTargetUpdateTask(false /* removeFile */);
    }

    if (!nodesToBeRemoved.isEmpty()) {
      // Wait for %age completion of the tablet move from master.
      createWaitForDataMoveTask().setSubTaskGroupType(SubTaskGroupType.WaitForDataMigration);
    } else {
      if (!tserversToBeRemoved.isEmpty()) {
        String errMsg = "Universe shrink should have been handled using node decommission.";
        log.error(errMsg);
        throw new IllegalStateException(errMsg);
      }
      if (confGetter.getConfForScope(universe, UniverseConfKeys.waitForLbForAddedNodes)
          && !newTservers.isEmpty()) {
        // If only tservers are added, wait for load to balance across all tservers.
        createWaitForLoadBalanceTask().setSubTaskGroupType(SubTaskGroupType.WaitForDataMigration);
      }
    }

    // Add new nodes to load balancer.
    createManageLoadBalancerTasks(
        createLoadBalancerMap(taskParams(), ImmutableList.of(cluster), null, null));

    if (cluster.clusterType == ClusterType.PRIMARY && isWaitForLeadersOnPreferred) {
      createWaitForLeadersOnPreferredOnlyTask();
    }

    if (!newMasters.isEmpty()) {

      // Filter out nodes which are not in the universe.
      Set<NodeDetails> removeUniverseMasters =
          filterUniverseNodes(universe, removeMasters, n -> true);
      // Now finalize the master quorum change tasks.
      createMoveMastersTasks(
          SubTaskGroupType.WaitForDataMigration, newMasters, removeUniverseMasters);

      if (!mastersToStop.isEmpty()) {
        createStopMasterTasks(mastersToStop)
            .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      }

      // Wait for a master leader to be elected.
      createWaitForMasterLeaderTask().setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Update these older ones to be not masters anymore so tserver info can be updated with the
      // final master list and other future cluster client operations.
      createUpdateNodeProcessTasks(removeMasters, ServerType.MASTER, false)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
    }

    if (updateMasters) {
      // Change the master addresses in the conf file for all tservers.
      Set<NodeDetails> allTservers = new HashSet<>(newTservers);
      allTservers.addAll(liveNodes);

      createConfigureServerTasks(
          allTservers,
          params -> {
            params.updateMasterAddrsOnly = true;
            params.ignoreUseCustomImageConfig = ignoreUseCustomImageConfig;
          });
      createUpdateMasterAddrsInMemoryTasks(allTservers, ServerType.TSERVER)
          .setSubTaskGroupType(SubTaskGroupType.UpdatingGFlags);

      Set<NodeDetails> allMasters = new HashSet<>(newMasters);
      Set<String> takenMasters =
          allMasters.stream().map(n -> n.nodeName).collect(Collectors.toSet());
      allMasters.addAll(
          liveNodes.stream()
              .filter(
                  n ->
                      n.isMaster
                          && !takenMasters.contains(n.nodeName)
                          && !mastersToStop.contains(n))
              .collect(Collectors.toSet()));

      // Change the master addresses in the conf file for the new masters.
      // Update the same set of master addresses.
      createConfigureServerTasks(
          allMasters,
          params -> {
            params.updateMasterAddrsOnly = true;
            params.isMaster = true;
            params.ignoreUseCustomImageConfig = ignoreUseCustomImageConfig;
          });
      createUpdateMasterAddrsInMemoryTasks(allMasters, ServerType.MASTER)
          .setSubTaskGroupType(SubTaskGroupType.UpdatingGFlags);

      // Update the master addresses on the target universes whose source universe belongs to
      // this task.
      createXClusterConfigUpdateMasterAddressesTask();
    }

    // Finally send destroy to the old set of nodes and remove them from this universe.
    if (!nodesToBeRemoved.isEmpty()) {
      // Remove nodes from load balancer.
      createManageLoadBalancerTasks(
          createLoadBalancerMap(taskParams(), ImmutableList.of(cluster), nodesToBeRemoved, null));

      // Set the node states to Removing.
      createSetNodeStateTasks(nodesToBeRemoved, NodeDetails.NodeState.Terminating)
          .setSubTaskGroupType(SubTaskGroupType.RemovingUnusedServers);
      createDestroyServerTasks(
              universe,
              nodesToBeRemoved,
              false /* isForceDelete */,
              true /* deleteNode */,
              true /* deleteRootVolumes */)
          .setSubTaskGroupType(SubTaskGroupType.RemovingUnusedServers);
    }

    if (!tserversToBeRemoved.isEmpty()) {
      // Clear blacklisted tservers.
      createModifyBlackListTask(null, tserversToBeRemoved, false /* isLeaderBlacklist */)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
    }
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
      createChangeConfigTasks(mastersToAdd.get(idx), true, subTask);
      createChangeConfigTasks(mastersToRemove.get(idx), false, subTask);
    }

    // Perform any additions still left.
    for (int idx = numIters; idx < newMasters.size(); idx++) {
      createChangeConfigTasks(mastersToAdd.get(idx), true, subTask);
    }

    // Perform any removals still left.
    for (int idx = numIters; idx < removeMasters.size(); idx++) {
      createChangeConfigTasks(mastersToRemove.get(idx), false, subTask);
    }
  }
}
