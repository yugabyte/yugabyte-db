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

import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.DnsManager.DnsCommandType;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Collections;
import java.util.Set;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

// Tracks the read only cluster create intent within an existing universe.
@Slf4j
@Abortable
@Retryable
public class ReadOnlyClusterCreate extends UniverseDefinitionTaskBase {

  @Inject
  protected ReadOnlyClusterCreate(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected void createPrecheckTasks(Universe universe) {
    if (isFirstTry()) {
      verifyClustersConsistency();
    }
  }

  @Override
  public void run() {
    log.info("Started {} task for uuid={}", getName(), taskParams().getUniverseUUID());

    try {
      // Set the 'updateInProgress' flag to prevent other updates from happening.
      Cluster cluster = taskParams().getReadOnlyClusters().get(0);
      Universe universe =
          lockAndFreezeUniverseForUpdate(
              taskParams().expectedUniverseVersion,
              u -> {
                // Fetch the task params from the DB to start from fresh on retry.
                // Otherwise, some operations like name assignment can fail.
                fetchTaskDetailsFromDB();
                preTaskActions(u);
                // Set all the in-memory node names.
                setNodeNames(u);
                // Set non on-prem node UUIDs.
                setCloudNodeUuids(u);
                // Update on-prem node UUIDs.
                updateOnPremNodeUuidsOnTaskParams();
                // Set the prepared data to universe in-memory.
                updateUniverseNodesAndSettings(u, taskParams(), true);
                u.getUniverseDetails()
                    .upsertCluster(cluster.userIntent, cluster.placementInfo, cluster.uuid);
                // There is a rare possibility that this succeeds and
                // saving the Universe fails. It is ok because the retry
                // will just fail.
                updateTaskDetailsInDB(taskParams());
              });

      // Sanity checks for clusters list validity are performed in the controller.
      Set<NodeDetails> readOnlyNodes = taskParams().getNodesInCluster(cluster.uuid);
      boolean ignoreUseCustomImageConfig = !readOnlyNodes.stream().allMatch(n -> n.ybPrebuiltAmi);

      // There should be no masters in read only clusters.
      if (!PlacementInfoUtil.getMastersToProvision(readOnlyNodes).isEmpty()) {
        String errMsg = "Cannot have master nodes in read-only cluster.";
        log.error("{} Nodes: {}", errMsg, readOnlyNodes);
        throw new IllegalArgumentException(errMsg);
      }

      Set<NodeDetails> nodesToProvision = PlacementInfoUtil.getNodesToProvision(readOnlyNodes);

      if (nodesToProvision.isEmpty()) {
        String errMsg = "Cannot have empty nodes to provision in read-only cluster.";
        log.error(errMsg);
        throw new IllegalArgumentException(errMsg);
      }

      // Create preflight node check tasks for on-prem nodes.
      createPreflightNodeCheckTasks(universe, Collections.singletonList(cluster));

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

      // Set of processes to be started, note that in this case it is same as nodes provisioned.
      Set<NodeDetails> newTservers = PlacementInfoUtil.getTserversToProvision(readOnlyNodes);

      // Make sure clock skew is low enough.
      createWaitForClockSyncTasks(universe, newTservers)
          .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);

      // Start the tservers in the clusters.
      createStartTserverProcessTasks(
          newTservers, universe.getUniverseDetails().getPrimaryCluster().userIntent.enableYSQL);

      // Start ybc process on all the nodes
      if (taskParams().isEnableYbc()) {
        createStartYbcProcessTasks(
            newTservers, universe.getUniverseDetails().getPrimaryCluster().userIntent.useSystemd);
        createUpdateYbcTask(taskParams().getYbcSoftwareVersion())
            .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      }

      // Set the node state to live.
      createSetNodeStateTasks(newTservers, NodeDetails.NodeState.Live)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Update the async_replicas in the cluster config on master leader.
      createPlacementInfoTask(null /* blacklistNodes */)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Add read replica nodes to the DNS entry.
      createDnsManipulationTask(DnsCommandType.Edit, false, universe)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Update the swamper target file.
      createSwamperTargetUpdateTask(false /* removeFile */);

      // Marks the update of this universe as a success only if all the tasks before it succeeded.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Run all the tasks.
      getRunnableTask().runSubTasks();
    } catch (Throwable t) {
      log.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);
      throw t;
    } finally {
      // Mark the update of the universe as done. This will allow future edits/updates to the
      // universe to happen.
      Universe universe = unlockUniverseForUpdate();
      if (universe.getConfig().getOrDefault(Universe.USE_CUSTOM_IMAGE, "false").equals("true")) {
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
}
