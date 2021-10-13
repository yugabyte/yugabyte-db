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

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Collection;
import java.util.Set;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

// Tracks the read only cluster create intent within an existing universe.
@Slf4j
public class ReadOnlyClusterCreate extends UniverseDefinitionTaskBase {

  @Inject
  protected ReadOnlyClusterCreate(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  public void run() {
    log.info("Started {} task for uuid={}", getName(), taskParams().universeUUID);

    try {
      // Create the task list sequence.
      subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);

      // Set the 'updateInProgress' flag to prevent other updates from happening.
      Universe universe = lockUniverseForUpdate(taskParams().expectedUniverseVersion);

      preTaskActions();

      // Set the correct node names for all to-be-added nodes.
      setNodeNames(UniverseOpType.CREATE, universe);

      // Update the user intent.
      universe = writeUserIntentToUniverse(true);
      updateOnPremNodeUuids(universe);

      // Sanity checks for clusters list validity are performed in the controller.
      Cluster cluster = taskParams().getReadOnlyClusters().get(0);
      Set<NodeDetails> readOnlyNodes = taskParams().getNodesInCluster(cluster.uuid);

      // There should be no masters in read only clusters.
      if (!PlacementInfoUtil.getMastersToProvision(readOnlyNodes).isEmpty()) {
        String errMsg = "Cannot have master nodes in read-only cluster.";
        log.error(errMsg + "Nodes : " + readOnlyNodes);
        throw new IllegalArgumentException(errMsg);
      }

      Collection<NodeDetails> nodesToProvision =
          PlacementInfoUtil.getNodesToProvision(readOnlyNodes);

      if (nodesToProvision.isEmpty()) {
        String errMsg = "Cannot have empty nodes to provision in read-only cluster.";
        log.error(errMsg);
        throw new IllegalArgumentException(errMsg);
      }

      // perform preflight checks for only readonly cluster
      performUniversePreflightChecks(universe, cl -> cl.uuid.equals(cluster.uuid));

      // Create the required number of nodes in the appropriate locations.
      createCreateServerTasks(nodesToProvision).setSubTaskGroupType(SubTaskGroupType.Provisioning);

      // Get all information about the nodes of the cluster. for ex., private ip address.
      createServerInfoTasks(nodesToProvision).setSubTaskGroupType(SubTaskGroupType.Provisioning);

      // Provision the nodes of the cluster so Yugabyte can be deployed.
      createSetupServerTasks(nodesToProvision).setSubTaskGroupType(SubTaskGroupType.Provisioning);

      // Configures and deploys software on all the nodes (masters and tservers).
      createConfigureServerTasks(nodesToProvision, true /* isShell */)
          .setSubTaskGroupType(SubTaskGroupType.InstallingSoftware);

      // Set of processes to be started, note that in this case it is same as nodes provisioned.
      Set<NodeDetails> newTservers = PlacementInfoUtil.getTserversToProvision(readOnlyNodes);

      // Set default gflags
      addDefaultGFlags(cluster.userIntent);
      createGFlagsOverrideTasks(newTservers, ServerType.TSERVER);

      // Start the tservers in the clusters.
      createStartTServersTasks(newTservers).setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Wait for all tablet servers to be responsive.
      createWaitForServersTasks(newTservers, ServerType.TSERVER)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Set the node state to live.
      createSetNodeStateTasks(newTservers, NodeDetails.NodeState.Live)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Update the async_replicas in the cluster config on master leader.
      createPlacementInfoTask(null /* blacklistNodes */)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Update the swamper target file.
      createSwamperTargetUpdateTask(false /* removeFile */);

      // Marks the update of this universe as a success only if all the tasks before it succeeded.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Run all the tasks.
      subTaskGroupQueue.run();
    } catch (Throwable t) {
      log.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);
      throw t;
    } finally {
      // Mark the update of the universe as done. This will allow future edits/updates to the
      // universe to happen.
      unlockUniverseForUpdate();
    }
    log.info("Finished {} task.", getName());
  }
}
