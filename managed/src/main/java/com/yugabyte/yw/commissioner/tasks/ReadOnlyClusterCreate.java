// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import java.util.ArrayList;
import java.util.Collection;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;

import com.yugabyte.yw.commissioner.SubTaskGroup;
import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

// Tracks the read only cluster create intent within an existing universe.
public class ReadOnlyClusterCreate extends UniverseDefinitionTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(ReadOnlyClusterCreate.class);

  @Override
  public void run() {
    LOG.info("Started {} task for uuid={}", getName(), taskParams().universeUUID);

    try {
      // Create the task list sequence.
      subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);

      // Set the 'updateInProgress' flag to prevent other updates from happening.
      Universe universe = lockUniverseForUpdate(taskParams().expectedUniverseVersion);

      // Update the user intent.
      writeUserIntentToUniverse(true /* isReadOnly */);

      // Set the correct node names for all to-be-added nodes.
      updateNodeNames();

      List<Cluster> newReadOnlyClusters = taskParams().getReadOnlyClusters();
      List<Cluster> existingReadOnlyClusters = universe.getUniverseDetails().getReadOnlyClusters();
      LOG.info("newReadOnly={}, existingRO={}.",
               newReadOnlyClusters.size(), existingReadOnlyClusters.size());

      if (existingReadOnlyClusters.size() > 0 && newReadOnlyClusters.size() > 0) {
        String errMsg = "Can only have one read-only cluster per universe for now.";
        LOG.error(errMsg);
        throw new IllegalArgumentException(errMsg);
      }

      if (newReadOnlyClusters.size() != 1) {
        String errMsg = "Only one read-only cluster expected, but we got " +
                        newReadOnlyClusters.size();
        LOG.error(errMsg);
        throw new IllegalArgumentException(errMsg);
      }

      Cluster cluster = newReadOnlyClusters.get(0);
      UUID uuid = cluster.uuid;
      if (uuid == null) {
        String errMsg = "UUID of read-only cluster should be non-null.";
        LOG.error(errMsg);
        throw new IllegalArgumentException(errMsg);
      }
      if (cluster.clusterType != ClusterType.ASYNC) {
        String errMsg = "Read-only cluster type should be " + ClusterType.ASYNC + " but is " +
                        cluster.clusterType;
        LOG.error(errMsg);
        throw new IllegalArgumentException(errMsg);
      }

      Set<NodeDetails> readOnlyNodes = taskParams().getNodesInCluster(uuid);

      // There should be no masters in read only clusters.
      if (!PlacementInfoUtil.getMastersToProvision(readOnlyNodes).isEmpty()) {
        String errMsg = "Cannot have master nodes in read-only cluster.";
        LOG.error(errMsg + "Nodes : " + readOnlyNodes);
        throw new IllegalArgumentException(errMsg);
      }

      Collection<NodeDetails> nodesToProvision =
        PlacementInfoUtil.getNodesToProvision(readOnlyNodes);

      if (nodesToProvision.isEmpty()) {
        String errMsg = "Cannot have empty nodes to provision in read-only cluster.";
        LOG.error(errMsg);
        throw new IllegalArgumentException(errMsg);
      }

      UserIntent userIntent = cluster.userIntent;

      // Create the required number of nodes in the appropriate locations.
      createSetupServerTasks(nodesToProvision, userIntent.deviceInfo)
          .setSubTaskGroupType(SubTaskGroupType.Provisioning);

      // Get all information about the nodes of the cluster. for ex., private ip address.
      createServerInfoTasks(nodesToProvision, userIntent.deviceInfo)
          .setSubTaskGroupType(SubTaskGroupType.Provisioning);

      // Configures and deploys software on all the nodes (masters and tservers).
      createConfigureServerTasks(nodesToProvision, true /* isShell */,
                                 userIntent.deviceInfo, userIntent.ybSoftwareVersion)
          .setSubTaskGroupType(SubTaskGroupType.InstallingSoftware);

      // Set of processes to be started, note that in this case it is same as nodes provisioned.
      Set<NodeDetails> newTservers = PlacementInfoUtil.getTserversToProvision(readOnlyNodes);

      // Start the tservers in the clusters.
      createStartTServersTasks(newTservers)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Wait for all tablet servers to be responsive.
      createWaitForServersTasks(newTservers, ServerType.TSERVER)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Update the async_replicas in the cluster config on master leader.
      createPlacementInfoTask(null /* blacklistNodes */)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Marks the update of this universe as a success only if all the tasks before it succeeded.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Run all the tasks.
      subTaskGroupQueue.run();
    } catch (Throwable t) {
      LOG.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);
      throw t;
    } finally {
      // Mark the update of the universe as done. This will allow future edits/updates to the
      // universe to happen.
      unlockUniverseForUpdate();
    }
    LOG.info("Finished {} task.", getName());
  }
}
